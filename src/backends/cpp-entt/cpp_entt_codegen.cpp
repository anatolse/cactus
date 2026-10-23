#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include "common/ast_expressions.hpp"
#include "common/codegen_error.hpp"
#include "frontend/delivery_cycles.hpp"
#include "frontend/hierarchy_forest.hpp"
#include "frontend/native_result_ordering.hpp"
#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/accumulation_plan.hpp"
#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/event_emitter.hpp"
#include "backends/cpp-entt/persistence_emitter.hpp"
#include "backends/cpp-entt/persistence_restore_emitter.hpp"
#include "backends/cpp-entt/range_checks.hpp"
#include "backends/cpp-entt/render_pass_emitter.hpp"
#include "backends/cpp-entt/system_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iomanip>
#include <limits>
#include <map>
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

const std::string& ast_event_module(const EventNode& node, const DecoratedProgram& program) {
    return node.module_name.empty() ? program.module_name : node.module_name;
}

std::string ast_event_cpp_name(const EventNode& node, const DecoratedProgram& program) {
    return cpp_type_ref(SymbolKind::Event, ast_event_module(node, program), node.name);
}

std::string resolved_event_cpp_name(const ResolvedEvent& event) {
    return cpp_type_ref(SymbolKind::Event, event.module_name, event.name);
}

template <typename Map>
std::vector<const typename Map::mapped_type*> sorted_by_canonical_id(const Map& declarations) {
    std::vector<const typename Map::mapped_type*> result;
    result.reserve(declarations.size());
    for (const auto& [_, declaration] : declarations) {
        result.push_back(&declaration);
    }
    std::ranges::sort(result, {}, &Map::mapped_type::canonical_id);
    return result;
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
            out << EnttEventEmitter::emit_event(*found->second, ast_event_module(*node, program)) << "\n";
        }
        return out.str();
    }

    for (const auto* event : sorted_by_canonical_id(program.events)) {
        out << EnttEventEmitter::emit_event(*event, event->module_name) << "\n";
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
    return cpp_type_ref(trigger.symbol);
}

// The namespace a user library defines an extern rule's `on` overloads in.
std::string external_rule_namespace(const SymbolId& rule) {
    return cpp_namespace_path(rule.module) + "::" + cpp_escaped(rule.local_name);
}

std::string external_handler_callback_name(const HandlerIdentity& identity) {
    return "::" + external_rule_namespace(identity.rule) + "::on";
}

std::vector<SymbolId> sorted_symbols(const std::unordered_set<SymbolId>& symbols) {
    std::vector<SymbolId> result(symbols.begin(), symbols.end());
    std::ranges::sort(
        result, [](const auto& left, const auto& right) { return make_canonical_id(left) < make_canonical_id(right); });
    return result;
}

// Harness-only: one hash of every entity's traits, entities in creation order
// and traits in canonical order, so two worlds compare by value.
std::string emit_world_digest(const DecoratedProgram& program) {
    std::ostringstream out;
    out << "#ifdef CACTUS_LINEARIZATION_HARNESS\n";
    out << "namespace cactus::runtime::entt_backend {\n";
    for (const auto* declaration : sorted_by_canonical_id(program.structs)) {
        out << "inline void digest_value(WorldDigest& digest, const "
            << cpp_type_ref(SymbolKind::Struct, declaration->module_name, declaration->name) << "& value) {\n";
        out << "    (void)digest;\n";
        for (const auto& field : declaration->fields) {
            out << "    digest.add(value." << field.name << ");\n";
        }
        out << "}\n";
    }
    const auto traits = sorted_by_canonical_id(program.traits);
    out << "std::uint64_t generated_world_digest(entt::registry& registry) {\n";
    out << "    WorldDigest digest;\n";
    out << "    for (const auto entity : creation_ordered(registry, registry.view<CreationOrdinal>())) {\n";
    out << "        digest.add(entt::to_integral(entity));\n";
    for (std::size_t index = 0; index < traits.size(); ++index) {
        const auto& trait = *traits[index];
        const auto cpp    = cpp_type_ref(SymbolKind::Trait, trait.module_name, trait.name);
        if (trait.fields.empty() && !trait.is_state_slot()) {
            out << "        if (registry.all_of<" << cpp << ">(entity)) {\n";
            out << "            digest.add(std::uint64_t{" << index << "});\n";
            out << "        }\n";
            continue;
        }
        out << "        if (const auto* component = registry.try_get<" << cpp << ">(entity)) {\n";
        out << "            digest.add(std::uint64_t{" << index << "});\n";
        if (trait.is_state_slot()) {
            out << "            digest.add(component->index);\n";
        }
        for (const auto& field : trait.fields) {
            out << "            digest.add(component->" << field.name << ");\n";
        }
        out << "        }\n";
    }
    out << "    }\n";
    out << "    return digest.value();\n";
    out << "}\n";
    out << "}  // namespace cactus::runtime::entt_backend\n";
    out << "#endif  // CACTUS_LINEARIZATION_HARNESS\n";
    return out.str();
}

std::vector<std::string> sorted_strings(const std::unordered_set<std::string>& strings) {
    std::vector<std::string> result(strings.begin(), strings.end());
    std::ranges::sort(result);
    return result;
}

std::string external_handler_capability_name(const DecoratedProgram& program, const HandlerIdentity& identity) {
    return "::" + external_rule_namespace(identity.rule) + "::Capabilities<" +
           handler_trigger_cpp_type(program, identity.trigger) + ">";
}

std::string external_handler_effect_method_name(const std::string& effect) {
    std::string name = "effect_";
    for (const char character : effect) {
        const bool kept = std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
        name += kept ? character : '_';
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
            return system_function_name(program.module_name, rule->name, "on").ref();
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

// What generated scheduling needs from the graph's delayed edges. An edge is
// delayed exactly when producer and consumer share a delivery cycle.
class DeliveryPlan {
public:
    explicit DeliveryPlan(const DecoratedProgram& program) : components_(delivery_components(program.execution_graph)) {
        const auto& graph = program.execution_graph;
        for (const auto& edge : graph.event_flows) {
            if (edge.delayed) {
                add(graph, {.kind = HandlerTriggerKind::Event, .symbol = edge.event}, edge.producer);
            }
        }
        for (const auto& edge : graph.lifecycle_flows) {
            if (edge.delayed) {
                const auto kind = edge.added ? HandlerTriggerKind::TraitAdded : HandlerTriggerKind::TraitRemoved;
                add(graph, {.kind = kind, .symbol = edge.trait}, edge.producer);
            }
        }
        for (const auto& depth : graph.activation_depths) {
            if (depth.kind == ActivationRootKind::Phase && depth.produces_delays && depth.symbol.has_value()) {
                delaying_phases_.insert(make_canonical_id(*depth.symbol));
            }
        }
    }

    [[nodiscard]] bool cyclic() const {
        return !components_.empty();
    }

    [[nodiscard]] std::string component_literal(const HandlerIdentity& handler) const {
        const auto found = components_.find(handler);
        return found == components_.end() ? "kNoComponent" : std::to_string(found->second) + "U";
    }

    // Producer cycle -> whether the trigger also has consumers off that cycle.
    [[nodiscard]] const std::map<std::uint32_t, bool>* delayed(const ResolvedHandlerTrigger& trigger) const {
        const auto found = delayed_.find(trigger.debug_string());
        return found == delayed_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] bool phase_delays(const SymbolId& phase) const {
        return delaying_phases_.contains(make_canonical_id(phase));
    }

private:
    void add(const ExecutionGraph& graph, const ResolvedHandlerTrigger& trigger, const HandlerIdentity& producer) {
        const auto component         = components_.at(producer);
        const auto [entry, inserted] = delayed_[trigger.debug_string()].try_emplace(component, false);
        if (!inserted) {
            return;
        }
        entry->second = std::ranges::any_of(graph.handlers, [&](const HandlerNode& consumer) {
            const auto found = components_.find(consumer.identity);
            return consumer.identity.trigger == trigger && (found == components_.end() || found->second != component);
        });
    }

    std::unordered_map<HandlerIdentity, std::uint32_t, HandlerIdentityHash> components_;
    std::map<std::string, std::map<std::uint32_t, bool>> delayed_;
    std::unordered_set<std::string> delaying_phases_;
};

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

// `requires (std::same_as<T, A> || ...)` over the traits a command may target.
std::string trait_constraint(const std::vector<SymbolId>& traits) {
    std::string constraint = "    template <class T>\n        requires(";
    for (std::size_t index = 0; index < traits.size(); ++index) {
        constraint += std::string(index == 0 ? "" : " || ") + "std::same_as<T, " +
                      EnttCodegenUtils::trait_cpp_name(traits[index]) + ">";
    }
    return constraint + ")\n";
}

// Per-trait statements inside a member template, each under `if constexpr (std::same_as<T, X>)`.
std::string per_trait_statements(const std::vector<SymbolId>& traits,
                                 const std::function<std::string(const SymbolId&, const std::string&)>& body,
                                 const std::string& indent) {
    std::string result;
    for (const auto& trait : traits) {
        const auto type       = EnttCodegenUtils::trait_cpp_name(trait);
        const auto statements = body(trait, type);
        if (statements.empty()) {
            continue;
        }
        result.append(indent).append("if constexpr (std::same_as<T, ").append(type).append(">) {\n");
        result.append(statements).append(indent).append("}\n");
    }
    return result;
}

std::vector<SymbolId> command_targets(const HandlerNode& node, HandlerCommandKind kind) {
    std::unordered_set<SymbolId> targets;
    for (const auto& command : node.contract.commands) {
        if (command.kind == kind && command.target.has_value()) {
            targets.insert(*command.target);
        }
    }
    return sorted_symbols(targets);
}

bool has_command(const HandlerNode& node, HandlerCommandKind kind) {
    return std::ranges::any_of(node.contract.commands, [kind](const auto& command) { return command.kind == kind; });
}

const TemplateNode& external_spawn_template(const DecoratedProgram& program, const SymbolId& target) {
    const auto* template_node = find_template(program, target);
    if (template_node == nullptr) {
        throw std::runtime_error("cpp-entt cannot lower external spawn capability for missing template '" +
                                 make_canonical_id(target) + "'");
    }
    if (creation_takes_parameters(template_node->initializers)) {
        throw std::runtime_error("cpp-entt cannot lower external spawn capability for parameterized template '" +
                                 make_canonical_id(target) +
                                 "': an external handler has no place to supply template arguments");
    }
    return *template_node;
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
        for (const auto& target : command_targets(node, HandlerCommandKind::Spawn)) {
            const auto factory =
                archetype_create_at_function_name(program.module_name, external_spawn_template(program, target).name);
            declarations.insert(namespace_block(
                factory.ns, "entt::entity " + factory.name + "(entt::registry& registry, entt::entity hint);\n"));
        }
    }
    if (EnttCodegenUtils::find_trait(program, "Parent") != nullptr) {
        declarations.insert(namespace_block("cactus_gen::detail",
                                            "[[maybe_unused]] static void cactus_destroy_entity_recursive("
                                            "entt::registry& registry, entt::entity entity);\n"));
    }
    if (declarations.empty()) {
        return {};
    }

    std::vector<std::string> ordered(declarations.begin(), declarations.end());
    std::ranges::sort(ordered);
    std::ostringstream out;
    out << "// ── External Command Target Declarations ─────────────────────────────\n\n";
    for (const auto& declaration : ordered) {
        out << declaration;
    }
    out << "\n";
    return out.str();
}

// The patch a `set<T>` command carries: optional fields keep the capability queue-only, so the
// callback names values to write and never sees the current ones.
std::string emit_external_set_patches(const DecoratedProgram& program) {
    std::unordered_set<SymbolId> targets;
    for (const auto& node : program.execution_graph.handlers) {
        if (is_user_external_handler(program, node)) {
            std::ranges::copy(command_targets(node, HandlerCommandKind::Set), std::inserter(targets, targets.end()));
        }
    }
    if (targets.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "template <class T>\n";
    out << "struct SetPatch;\n\n";
    for (const auto& target : sorted_symbols(targets)) {
        const auto type            = EnttCodegenUtils::trait_cpp_name(target);
        const auto* resolved_trait = EnttCodegenUtils::find_trait(program, make_canonical_id(target));
        if (resolved_trait == nullptr) {
            throw std::runtime_error("cpp-entt cannot lower external set capability for missing trait '" +
                                     make_canonical_id(target) + "'");
        }
        out << "template <>\n";
        out << "struct SetPatch<" << type << "> {\n";
        for (const auto& field : resolved_trait->fields) {
            const auto member = cpp_escaped(field.name);
            out << "    std::optional<decltype(" << type << "::" << member << ")> " << member << ";\n";
        }
        out << "    void apply(" << type << "& value) const {\n";
        for (const auto& field : resolved_trait->fields) {
            const auto member = cpp_escaped(field.name);
            out << "        if (" << member << ") { value." << member << " = *" << member << "; }\n";
        }
        out << "    }\n";
        out << "};\n\n";
    }
    return namespace_block("cactus_gen::detail", out.str());
}

void emit_external_project_capability(std::ostringstream& out,
                                      const DecoratedProgram& program,
                                      const HandlerNode& node) {
    const auto traits = sorted_symbols(node.contract.projects);
    if (traits.empty()) {
        return;
    }
    for (const auto& trait : traits) {
        if (EnttCodegenUtils::find_trait(program, make_canonical_id(trait)) == nullptr) {
            throw std::runtime_error("cpp-entt cannot lower external project capability for missing trait '" +
                                     make_canonical_id(trait) + "'");
        }
    }
    out << trait_constraint(traits);
    out << "    [[nodiscard]] auto project(entt::entity target) const {\n";
    out << "        if constexpr (std::is_empty_v<T>) {\n";
    out << "            if (!registry.valid(target)) { return false; }\n";
    out << "            ::cactus_gen::detail::project<T>(registry, target);\n";
    out << "            return true;\n";
    out << "        } else {\n";
    out << "            if (!registry.valid(target)) { return static_cast<T*>(nullptr); }\n";
    out << "            return &::cactus_gen::detail::project<T>(registry, target);\n";
    out << "        }\n";
    out << "    }\n";
}

void emit_external_spawn_capability(std::ostringstream& out, const DecoratedProgram& program, const HandlerNode& node) {
    for (const auto& target : command_targets(node, HandlerCommandKind::Spawn)) {
        const auto& template_node = external_spawn_template(program, target);
        const auto factory        = archetype_create_at_function_name(program.module_name, template_node.name);
        out << "    [[nodiscard]] entt::entity spawn_" << snake_case(template_node.name) << "() const {\n";
        out << "        const auto entity = ::cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        out << "        ::cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "            ::cactus::runtime::entt_backend::StructuralCommand::Kind::Spawn,\n";
        out << "            [entity](entt::registry& registry) { (void)" << factory.ref()
            << "(registry, entity); });\n";
        out << "        return entity;\n";
        out << "    }\n";
    }
}

void emit_external_destroy_capability(std::ostringstream& out,
                                      const DecoratedProgram& program,
                                      const HandlerNode& node) {
    if (!has_command(node, HandlerCommandKind::Destroy)) {
        return;
    }
    out << "    void destroy(entt::entity target) const {\n";
    out << "        ::cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    out << "            ::cactus::runtime::entt_backend::StructuralCommand::Kind::Destroy,\n";
    out << "            [target](entt::registry& registry) {\n";
    out << "                if (!registry.valid(target)) { return; }\n";
    if (EnttCodegenUtils::find_trait(program, "Parent") != nullptr) {
        out << "                ::cactus_gen::detail::cactus_destroy_entity_recursive(registry, target);\n";
    } else {
        out << "                cactus::runtime::entt_backend::release_entity(registry, target);\n";
    }
    out << "            });\n";
    out << "    }\n";
}

void emit_external_add_capability(std::ostringstream& out, const DecoratedProgram& program, const HandlerNode& node) {
    const auto traits = command_targets(node, HandlerCommandKind::Add);
    if (traits.empty()) {
        return;
    }
    out << trait_constraint(traits);
    out << "    void add(entt::entity target, T value = {}) const {\n";
    out << "        ::cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    out << "            ::cactus::runtime::entt_backend::StructuralCommand::Kind::Add,\n";
    out << "            [target, value = std::move(value)](entt::registry& registry) mutable {\n";
    out << "                if (!registry.valid(target)) { return; }\n";
    out << per_trait_statements(
        traits,
        [&](const SymbolId& trait, const std::string&) -> std::string {
            if (!is_hierarchy_trait(trait) || !EnttCodegenUtils::checks_reparent(program)) {
                return {};
            }
            return "                    if (cactus::runtime::entt_backend::reparent_would_cycle(" +
                   EnttCodegenUtils::hierarchy_index_expr(program) + ", target, value.parent)) { return; }\n";
        },
        "                ");
    out << "                ::cactus_gen::detail::cancel_projected<T>(target);\n";
    out << "                registry.emplace_or_replace<T>(target, std::move(value));\n";
    out << per_trait_statements(
        traits,
        [&](const SymbolId& trait, const std::string& type) -> std::string {
            if (!EnttPersistenceEmitter::structural_site_tracks_construction(program, trait, trait.local_name)) {
                return {};
            }
            return EnttPersistenceEmitter::emit_retain_construction(type, "target", "                    ");
        },
        "                ");
    out << "            });\n";
    out << "    }\n";
}

void emit_external_remove_capability(std::ostringstream& out,
                                     const DecoratedProgram& program,
                                     const HandlerNode& node) {
    const auto traits = command_targets(node, HandlerCommandKind::Remove);
    if (traits.empty()) {
        return;
    }
    out << trait_constraint(traits);
    out << "    void remove(entt::entity target) const {\n";
    out << "        ::cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    out << "            ::cactus::runtime::entt_backend::StructuralCommand::Kind::Remove,\n";
    out << "            [target](entt::registry& registry) {\n";
    out << "                if (!registry.valid(target)) { return; }\n";
    out << "                ::cactus_gen::detail::cancel_projected<T>(target);\n";
    out << "                if (registry.all_of<T>(target)) { registry.remove<T>(target); }\n";
    out << per_trait_statements(
        traits,
        [&](const SymbolId& trait, const std::string& type) -> std::string {
            if (!EnttPersistenceEmitter::structural_site_tracks_construction(program, trait, trait.local_name)) {
                return {};
            }
            return EnttPersistenceEmitter::emit_discard_construction(type, "target", "                    ");
        },
        "                ");
    out << "            });\n";
    out << "    }\n";
}

void emit_external_set_capability(std::ostringstream& out, const HandlerNode& node) {
    const auto traits = command_targets(node, HandlerCommandKind::Set);
    if (traits.empty()) {
        return;
    }
    out << trait_constraint(traits);
    out << "    void set(entt::entity target, ::cactus_gen::detail::SetPatch<T> patch) const {\n";
    out << "        ::cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    out << "            ::cactus::runtime::entt_backend::StructuralCommand::Kind::Set,\n";
    out << "            [target, patch = std::move(patch)](entt::registry& registry) {\n";
    out << "                auto* value = ::cactus_gen::detail::durable<T>(registry, target);\n";
    out << "                if (value == nullptr) { return; }\n";
    out << "                patch.apply(*value);\n";
    out << "            });\n";
    out << "    }\n";
}

// Effect names are dotted strings with no type to key on; two that flatten alike are an authoring error.
void emit_external_effect_capability(std::ostringstream& out,
                                     const DecoratedProgram& program,
                                     const HandlerNode& node) {
    std::unordered_map<std::string, std::string> effect_by_method;
    for (const auto& effect : sorted_strings(node.contract.effects)) {
        const auto method               = external_handler_effect_method_name(effect);
        const auto [existing, inserted] = effect_by_method.emplace(method, effect);
        if (!inserted) {
            const auto* rule = find_external_rule(program, node.identity.rule);
            throw CodegenError(rule != nullptr ? rule->location : SourceLocation{},
                               std::format("effects '{}' and '{}' both lower to the capability method '{}'; "
                                           "rename one of them",
                                           existing->second,
                                           effect,
                                           method));
        }
        out << "    [[nodiscard]] ::cactus::runtime::entt_backend::EffectService " << method << "() const noexcept {\n";
        out << "        return ::cactus::runtime::entt_backend::EffectService{.domain = \"" << effect << "\"};\n";
        out << "    }\n";
    }
}

void emit_external_capability(std::ostringstream& out, const DecoratedProgram& program, const HandlerNode& node) {
    out << "template <>\n";
    out << "struct Capabilities<" << handler_trigger_cpp_type(program, node.identity.trigger) << "> {\n";
    out << "    entt::registry& registry;\n";
    emit_external_project_capability(out, program, node);
    for (const auto& event : sorted_symbols(node.contract.emits)) {
        out << "    void emit(" << event_runtime_cpp_type(program, event) << " occurrence) const {\n";
        out << "        ::cactus::runtime::entt_backend::generated_emit_event(std::move(occurrence));\n";
        out << "    }\n";
    }
    emit_external_spawn_capability(out, program, node);
    emit_external_destroy_capability(out, program, node);
    emit_external_add_capability(out, program, node);
    emit_external_remove_capability(out, program, node);
    emit_external_set_capability(out, node);
    emit_external_effect_capability(out, program, node);
    out << "};\n\n";
}

// The `on` overload a user library defines for one handler; only the trigger is named.
void emit_external_callback_declaration(std::ostringstream& out,
                                        const DecoratedProgram& program,
                                        const HandlerNode& node) {
    const auto trigger_type = handler_trigger_cpp_type(program, node.identity.trigger);
    out << "void on(const " << trigger_type << "& trigger";
    if (!node.contract.is_selectionless()) {
        out << ", entt::entity entity";
    }
    for (const auto& trait : sorted_symbols(node.contract.reads)) {
        if (!node.contract.writes.contains(trait)) {
            out << ", const " << EnttCodegenUtils::trait_cpp_name(trait) << "&";
        }
    }
    for (const auto& trait : sorted_symbols(node.contract.writes)) {
        out << ", " << EnttCodegenUtils::trait_cpp_name(trait) << "&";
    }
    out << ", const Capabilities<" << trigger_type << ">& capabilities);\n\n";
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
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    out << emit_external_set_patches(program);

    std::vector<SymbolId> rules;
    for (const auto& node : program.execution_graph.handlers) {
        if (is_user_external_handler(program, node) && !std::ranges::contains(rules, node.identity.rule)) {
            rules.push_back(node.identity.rule);
        }
    }
    for (const auto& rule : rules) {
        std::ostringstream body;
        body << "template <class Trigger>\n";
        body << "struct Capabilities;\n\n";
        for (const auto& node : program.execution_graph.handlers) {
            if (node.identity.rule == rule && is_user_external_handler(program, node)) {
                emit_external_capability(body, program, node);
                emit_external_callback_declaration(body, program, node);
            }
        }
        out << namespace_block(external_rule_namespace(rule), body.str()) << "\n";
    }
    return out.str();
}

// target_expr, when non-empty, names a `std::optional<entt::entity>` C++
// expression in scope: a unary external handler then runs at most once, for
// that recipient, gated on it satisfying the handler's selection, instead of
// broadcasting across the full matching view (targeted-event-delivery). Empty
// means no recipient concept applies at this call site (phase dispatch).
void emit_external_handler_call(std::ostringstream& out,
                                const DecoratedProgram& program,
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
        out << callback_indent << external_handler_callback_name(node.identity) << "(" << trigger;
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
        out << ", " << external_handler_capability_name(program, node.identity) << "{registry});\n";
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

void emit_viewport_phase_dispatch(std::ostringstream& out,
                                  const std::string& indent,
                                  bool render_uses_viewport,
                                  bool render_emit_2d_helper,
                                  bool render_emit_3d_helper,
                                  const std::string& render_vp_cpp,
                                  const std::string& render_cam2d_cpp,
                                  const std::string& render_cam3d_cpp) {
    if (!render_uses_viewport || (!render_emit_2d_helper && !render_emit_3d_helper)) {
        out << indent << "generated_dispatch_phase(registry, phase);\n";
        return;
    }
    out << indent << "{\n";
    out << indent << "    const int cactus_gen_sw = cactus::runtime::raylib::GetScreenWidth();\n";
    out << indent << "    const int cactus_gen_sh = cactus::runtime::raylib::GetScreenHeight();\n";
    out << indent << "    static std::vector<std::pair<int,entt::entity>> cactus_gen_vps;\n";
    out << indent << "    cactus_gen_vps.clear();\n";
    out << indent << "    for (const auto& [cactus_gen_vp_e, cactus_gen_vp] : registry.view<" << render_vp_cpp << ">().each()) {\n";
    out << indent << "        if (cactus_gen_vp.active) { cactus_gen_vps.emplace_back(cactus_gen_vp.depth, cactus_gen_vp_e); }\n";
    out << indent << "    }\n";
    out << indent << "    std::ranges::sort(cactus_gen_vps);\n";
    out << indent << "    for (auto& [cactus_gen_depth, cactus_gen_vp_ent] : cactus_gen_vps) {\n";
    out << indent << "        (void)cactus_gen_depth;\n";
    out << indent << "        const auto& cactus_gen_vp = registry.get<" << render_vp_cpp << ">(cactus_gen_vp_ent);\n";
    out << indent << "        cactus::runtime::raylib::BeginScissorMode(\n";
    out << indent << "            static_cast<int>(cactus_gen_vp.x * static_cast<float>(cactus_gen_sw)),\n";
    out << indent << "            static_cast<int>(cactus_gen_vp.y * static_cast<float>(cactus_gen_sh)),\n";
    out << indent << "            static_cast<int>(cactus_gen_vp.width * static_cast<float>(cactus_gen_sw)),\n";
    out << indent << "            static_cast<int>(cactus_gen_vp.height * static_cast<float>(cactus_gen_sh)));\n";
    out << indent << "        if (cactus_gen_vp.clear) { cactus::runtime::raylib::ClearBackground(cactus_gen_vp.clear_color); }\n";
    if (render_emit_2d_helper) {
        out << indent << "        if (registry.all_of<" << render_cam2d_cpp << ">(cactus_gen_vp_ent)) {\n";
        out << indent << "            const auto& cactus_gen_cam = registry.get<" << render_cam2d_cpp << ">(cactus_gen_vp_ent);\n";
        out << indent << "            set_active_camera_2d(cactus_gen_translate_camera_2d(cactus_gen_cam, cactus_gen_sw, cactus_gen_sh));\n";
        out << indent << "        }\n";
    }
    if (render_emit_3d_helper) {
        out << indent << (render_emit_2d_helper ? "        else if (registry.all_of<" : "        if (registry.all_of<")
            << render_cam3d_cpp << ">(cactus_gen_vp_ent)) {\n";
        out << indent << "            const auto& cactus_gen_cam = registry.get<" << render_cam3d_cpp << ">(cactus_gen_vp_ent);\n";
        out << indent << "            set_active_camera_3d(cactus_gen_translate_camera_3d(cactus_gen_vp_ent, cactus_gen_cam, registry));\n";
        out << indent << "        }\n";
    }
    out << indent << "        cactus::runtime::entt_backend::set_active_viewport_entity(cactus_gen_vp_ent);\n";
    out << indent << "        generated_dispatch_phase(registry, phase);\n";
    out << indent << "        flush_viewport_3d();\n";
    out << indent << "        cactus::runtime::raylib::EndScissorMode();\n";
    out << indent << "    }\n";
    out << indent << "    cactus::runtime::entt_backend::set_active_viewport_entity(entt::null);\n";
    out << indent << "}\n";
}

// A phase's runtime state in the scheduler, keyed by its type.
std::string phase_state(const std::string& phase_type) {
    return "std::get<" + phase_type + ">(scheduler.phases)";
}

// The delayed queue of a phase's clock, keyed by the phase type.
std::string phase_delayed_queue(const std::string& phase_type) {
    return "std::get<PhaseDelayedQueue<" + phase_type + ">>(scheduler.delayed)";
}

bool is_trivially_copyable(const ResolvedEvent& event, const DecoratedProgram& program) {
    return std::ranges::all_of(event.fields, [&](const ResolvedField& field) {
        return EnttCodegenUtils::is_trivially_copyable(field.type, program);
    });
}

void emit_external_event_injector(std::ostringstream& out, const ResolvedEvent& event, const DecoratedProgram& program) {
    const bool copy = is_trivially_copyable(event, program);
    out << "void generated_inject_external_event(" << event_runtime_cpp_type(program, *event.symbol_id)
        << " occurrence) {\n";
    out << "    generated_scheduler_state().activation.root_event_queue.push_back(\n";
    out << "        QueuedEvent<EventOccurrence>{.occurrence = " << (copy ? "occurrence" : "std::move(occurrence)")
        << "});\n";
    out << "}\n\n";
}

std::string phase_batch_tag(const std::string& phase_type) {
    return "std::type_identity<" + phase_type + ">";
}

// Copies the phase's root-bound and upstream-bound fields into its runtime state.
void emit_phase_field_bindings(std::ostringstream& out, const auto* phase) {
    bool used_root_event = false;
    for (const auto& field : phase->fields) {
        if (!field.source_binding.has_value()) {
            continue;
        }
        const auto& binding   = *field.source_binding;
        const auto field_type = EnttCodegenUtils::value_type_to_cpp(field.type);
        if (binding.kind == PhaseFieldSource::Kind::RootEvent) {
            used_root_event = true;
            out << "    phase." << field.name << " = static_cast<" << field_type << ">(root_event." << binding.member
                << ");\n";
        } else {
            out << "    phase." << field.name << " = static_cast<" << field_type << ">("
                << phase_state(cpp_type_ref(binding.source)) << "." << binding.member << ");\n";
        }
    }
    if (!used_root_event) {
        out << "    (void)root_event;\n";
    }
}

void emit_phase_batch(std::ostringstream& out,
                      const DecoratedProgram& program,
                      const DeliveryPlan& delivery,
                      const auto* phase,
                      const auto* render_phase,
                      const auto& emit_render_phase_dispatch) {
    if (!phase->runtime_root.has_value()) {
        return;
    }
    const auto phase_type            = cpp_type_ref(phase->phase);
    const auto root_type             = event_runtime_cpp_type(program, *phase->runtime_root);
    const bool is_render_phase_batch = phase == render_phase;
    const bool delays                = delivery.phase_delays(phase->phase);
    // One activation of the phase: one tick of its clock.
    const auto emit_activation = [&](const std::string& indent) {
        out << indent << "scheduler.activation.active = true;\n";
        if (delivery.cyclic()) {
            out << indent << "scheduler.activation.current_delayed = "
                << (delays ? "&" + phase_delayed_queue(phase_type) : std::string("nullptr")) << ";\n";
        }
        if (is_render_phase_batch) {
            out << indent << "begin_render_frame();\n";
            emit_render_phase_dispatch(indent);
            out << indent << "end_render_frame();\n";
        } else {
            out << indent << "generated_dispatch_phase(registry, phase);\n";
        }
        if (delays) {
            out << indent << "deliver_delayed_events(scheduler.activation);\n";
        }
        out << indent << "generated_drain_event_cascade(registry);\n";
        out << indent << "generated_commit_activation(registry);\n";
        out << indent << "scheduler.activation.active = false;\n";
    };
    out << "void generated_dispatch_phase(entt::registry& registry, const " << phase_type << "& phase);\n\n";
    out << "void generated_run_phase_batch(entt::registry& registry, const " << root_type << "& root_event, "
        << "[[maybe_unused]] " << phase_batch_tag(phase_type) << " tag) {\n";
    out << "    auto& scheduler = generated_scheduler_state();\n";
    out << "    auto& phase = " << phase_state(phase_type) << ";\n";
    if (!phase->every_seconds.has_value()) {
        emit_phase_field_bindings(out, phase);
        emit_activation("    ");
        out << "    ++phase.completed_batches;\n";
        out << "}\n\n";
        return;
    }
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
    out << "    phase.dt = static_cast<float>(interval);\n";
    out << "    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {\n";
    emit_activation("        ");
    out << "    }\n";
    out << "    phase.accumulator -= static_cast<double>(due) * interval;\n";
    out << "    phase.accumulator = std::max(phase.accumulator, 0.0);\n";
    out << "    if (phase.accumulator >= interval) { phase.accumulator = std::fmod(phase.accumulator, "
           "interval); }\n";
    out << "    phase.alpha = phase.accumulator / interval;\n";
    out << "    ++phase.completed_batches;\n";
    out << "}\n\n";
}

void emit_root_event_processor(std::ostringstream& out,
                               const DecoratedProgram& program,
                               const ResolvedEvent* event,
                               const ResolvedEvent* frame_event,
                               const auto* render_phase,
                               const auto& phase_order,
                               const auto& emit_own_activation) {
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
            out << "    generated_run_phase_batch(registry, root_event, " << phase_batch_tag(cpp_type_ref(phase->phase))
                << "{});\n";
            // Projected-trait cleanup fires once per frame, immediately
            // after the render phase batch completes.
            if (is_frame_root && phase == render_phase) {
                out << "    ::cactus_gen::detail::clear_projected_traits(registry);\n";
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

std::vector<const ResolvedEvent*> sorted_events(const DecoratedProgram& program) {
    std::vector<const ResolvedEvent*> events;
    for (const auto& [_, event] : program.events) {
        if (event.symbol_id.has_value()) {
            events.push_back(&event);
        }
    }
    std::ranges::sort(events,
                      [](const auto* left, const auto* right) { return left->canonical_id < right->canonical_id; });
    return events;
}

void emit_render_phase_dispatch(std::ostringstream& out,
                                const DecoratedProgram& program,
                                const auto* render_phase,
                                const std::string& indent) {
    // Mirrors the same flag/name computation CppEnttCodegen::generate() uses
    // for the legacy per-viewport render loop and edit-mode HUD overlay (see
    // the early cactus_gen_translate_camera_2d/3d helper emission there) — these are
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

    emit_viewport_phase_dispatch(out,
                                 indent,
                                 render_uses_viewport,
                                 render_emit_2d_helper,
                                 render_emit_3d_helper,
                                 render_vp_cpp,
                                 render_cam2d_cpp,
                                 render_cam3d_cpp);
}

void emit_phase_runtime_states(std::ostringstream& out, const DecoratedProgram& program) {
    for (const auto& phase : program.execution_graph.phases) {
        std::ostringstream body;
        body << "struct " << cpp_decl_name(phase.phase) << " {\n";
        if (phase.every_seconds.has_value()) {
            body << "    double accumulator{};\n";
            body << "    double alpha{};\n";
        }
        body << "    std::uint64_t completed_batches{};\n";
        for (const auto& field : phase.fields) {
            if (field.is_completion_only) {
                continue;
            }
            body << "    " << EnttCodegenUtils::value_type_to_cpp(field.type) << " " << field.name
                 << EnttCodegenUtils::value_initializer(field.type) << ";\n";
        }
        body << "};\n";
        out << namespace_block(cpp_namespace_path(phase.phase.module), body.str()) << "\n";
    }
}

void emit_stable_handler_table(std::ostringstream& out, const DecoratedProgram& program) {
    out << "struct StableHandlerDispatch {\n";
    out << "    std::string_view canonical_id;\n";
    out << "    bool selectionless;\n";
    out << "};\n";
    out << "inline constexpr std::array<StableHandlerDispatch, "
        << program.execution_graph.stable_topological_order.size() << "> kStableHandlerDispatch = {{\n";
    for (const auto& identity : program.execution_graph.stable_topological_order) {
        const auto found = std::ranges::find_if(program.execution_graph.handlers,
                                                [&](const auto& handler) { return handler.identity == identity; });
        out << "    {.canonical_id = \"" << identity.canonical_id() << "\", .selectionless = "
            << (found != program.execution_graph.handlers.end() && found->contract.is_selectionless() ? "true"
                                                                                                      : "false")
            << "},\n";
    }
    out << "}};\n\n";
}

// One emit call per route of an occurrence whose producer is on cycle `component`.
void emit_routed_emit_case(std::ostringstream& out, std::uint32_t component, bool outside, bool targeted) {
    const auto literal   = std::to_string(component) + "U";
    const auto recipient = std::string(targeted ? "target, " : "");
    const auto now       = std::string(targeted ? "emit_targeted_event" : "emit_event");
    const auto later     = std::string(targeted ? "emit_delayed_targeted_event" : "emit_delayed_event");
    out << "        case " << literal << ":\n";
    if (outside) {
        out << "            " << now << "(activation, occurrence, " << recipient << "DeliveryRoute::ImmediateOnly, "
            << literal << ");\n";
    }
    out << "            " << later << "(activation, std::move(occurrence), " << recipient
        << "DeliveryRoute::DelayedOnly, " << literal << ");\n";
    out << "            return;\n";
}

// For each event some cycle delays: emit overloads that route by the running
// handler's cycle, preferred over the generic templates by overload resolution.
void emit_delayed_emit_overloads(std::ostringstream& out,
                                 const DecoratedProgram& program,
                                 const DeliveryPlan& delivery,
                                 const std::vector<const ResolvedEvent*>& events) {
    for (const auto* event : events) {
        const auto* delayed =
            delivery.delayed(ResolvedHandlerTrigger{.kind = HandlerTriggerKind::Event, .symbol = *event->symbol_id});
        if (delayed == nullptr) {
            continue;
        }
        const auto type = event_runtime_cpp_type(program, *event->symbol_id);
        for (const bool targeted : {false, true}) {
            out << "void " << (targeted ? "generated_emit_targeted_event(" : "generated_emit_event(") << type
                << " occurrence" << (targeted ? ", entt::entity target" : "") << ") {\n";
            out << "    auto& activation = generated_scheduler_state().activation;\n";
            out << "    switch (activation.current_component) {\n";
            for (const auto& [component, outside] : *delayed) {
                emit_routed_emit_case(out, component, outside, targeted);
            }
            out << "        default:\n";
            out << "            " << (targeted ? "emit_targeted_event(activation, std::move(occurrence), target);\n"
                                             : "emit_event(activation, std::move(occurrence));\n");
            out << "    }\n";
            out << "}\n\n";
        }
    }
}

// Which consumers of each watched trait's triggers a producer cycle reaches by delay.
void emit_lifecycle_routes(std::ostringstream& out, const DecoratedProgram& program, const DeliveryPlan& delivery) {
    out << "struct GeneratedLifecycleRoutes {\n";
    out << "    [[nodiscard]] LifecycleSplit operator()(std::size_t trait_index, bool added, std::uint32_t component) "
           "const {\n";
    const auto traits = EnttCodegenUtils::watched_lifecycle_traits(program);
    for (std::size_t index = 0; index < traits.size(); ++index) {
        for (const bool added : {true, false}) {
            const auto kind     = added ? HandlerTriggerKind::TraitAdded : HandlerTriggerKind::TraitRemoved;
            const auto* delayed = delivery.delayed(ResolvedHandlerTrigger{.kind = kind, .symbol = traits[index]});
            if (delayed == nullptr) {
                continue;
            }
            for (const auto& [component, outside] : *delayed) {
                out << "        if (trait_index == " << index << " && " << (added ? "added" : "!added")
                    << " && component == " << component << "U) {\n";
                out << "            return {.immediate = " << (outside ? "true" : "false") << ", .delayed = true};\n";
                out << "        }\n";
            }
        }
    }
    out << "        return {};\n";
    out << "    }\n";
    out << "};\n\n";
}

// Per-phase runtime state, plus one delayed queue per clock that can delay, each keyed by phase type.
void emit_scheduler_state_struct(std::ostringstream& out,
                                 const DecoratedProgram& program,
                                 const DeliveryPlan& delivery) {
    std::vector<std::string> phases;
    std::vector<std::string> delayed;
    for (const auto& phase : program.execution_graph.phases) {
        phases.push_back(cpp_type_ref(phase.phase));
        if (delivery.phase_delays(phase.phase)) {
            delayed.push_back("PhaseDelayedQueue<" + phases.back() + ">");
        }
    }
    if (!delayed.empty()) {
        out << "template <class Phase>\n";
        out << "struct PhaseDelayedQueue : DelayedQueue<EventOccurrence> {};\n\n";
    }
    out << "struct SchedulerState {\n";
    out << "    ActivationRuntime<EventOccurrence> activation;\n";
    out << "    std::tuple<" << join_strings(phases, ", ") << "> phases;\n";
    if (!delayed.empty()) {
        out << "    std::tuple<" << join_strings(delayed, ", ") << "> delayed;\n";
    }
    if (delivery.cyclic()) {
        out << "    DelayedQueue<EventOccurrence> delayed_host;\n";
    }
    out << "};\n\n";
}

// The load/unload boundary helper and the external-event drain, which runs
// host-clock delays as activations of their own.
void emit_host_clock_entry_points(std::ostringstream& out, const DeliveryPlan& delivery, const DecoratedProgram& program) {
    // The load and unload boundaries: one host-clock activation each.
    out << "template <typename Occurrence>\n";
    out << "void generated_run_boundary_activation(entt::registry& registry, const Occurrence& occurrence) {\n";
    out << "    auto& activation = generated_scheduler_state().activation;\n";
    out << "    activation.active = true;\n";
    if (delivery.cyclic()) {
        out << "    activation.current_delayed = &generated_scheduler_state().delayed_host;\n";
    }
    out << "    generated_dispatch_event(registry, occurrence);\n";
    out << "    generated_drain_event_cascade(registry);\n";
    out << "    generated_commit_activation(registry);\n";
    // A SaveRequested queued by the boundary is processed at the boundary too.
    out << "    generated_process_persistence_boundary(registry);\n";
    out << "    activation.active = false;\n";
    out << "}\n\n";

    out << "void generated_drain_external_events(entt::registry& registry) {\n";
    out << "    auto& activation = generated_scheduler_state().activation;\n";
    out << "    auto& queue = activation.root_event_queue;\n";
    // Only delays queued before this drain run in it, so one drain always ends.
    if (delivery.cyclic()) {
        out << "    auto delayed = std::move(generated_scheduler_state().delayed_host);\n";
        out << "    generated_scheduler_state().delayed_host.clear();\n";
    }
    out << "    while (!queue.empty()) {\n";
    const bool copy = std::ranges::all_of(sorted_events(program),
                                          [&](const auto* event) { return is_trivially_copyable(*event, program); });
    out << "        auto queued = " << (copy ? "queue.front()" : "std::move(queue.front())") << ";\n";
    out << "        queue.pop_front();\n";
    out << "        std::visit([&](const auto& occurrence) { generated_process_root_event(registry, occurrence, queued.target); },\n";
    out << "                   queued.occurrence);\n";
    out << "    }\n";
    if (delivery.cyclic()) {
        out << "    for (auto& queued : delayed) {\n";
        out << "        if (!deliverable(registry, queued)) {\n";
        out << "            continue;\n";
        out << "        }\n";
        out << "        begin_delivery(activation, queued);\n";
        out << "        std::visit([&](const auto& occurrence) { generated_process_root_event(registry, occurrence, queued.target); },\n";
        out << "                   queued.occurrence);\n";
        out << "        end_delivery(activation);\n";
        out << "    }\n";
    }
    // Persistence requests are processed only once this drain's own root
    // events (and everything their handlers cascaded/committed) are fully
    // settled — the boundary decision-6 requires. generated_process_persistence_boundary
    // itself guards against the reentrant nested call this makes when it
    // delivers each outcome as its own activation.
    out << "    generated_process_persistence_boundary(registry);\n";
    out << "}\n\n";
}

std::string emit_graph_scheduler_state(const DecoratedProgram& program,
                                       const AccumulationPlan& accumulation,
                                       const DeliveryPlan& delivery) {
    if (program.execution_graph.phases.empty()) {
        return {};
    }

    const auto all_events = sorted_events(program);
    std::vector<const ResolvedEvent*> external_events;
    std::ranges::copy_if(
        all_events, std::back_inserter(external_events), [](const auto* event) { return event->is_external; });

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

    std::ostringstream out;
    out << "// ── Graph Activation Runtime State ──────────────────────────────────\n\n";
    emit_phase_runtime_states(out, program);
    out << "namespace cactus::runtime::entt_backend {\n\n";
    // QueuedEvent<Occurrence> and ActivationRuntime<Occurrence> are
    // runtime-hosted (backends/cpp-entt/runtime.hpp) — EventOccurrence
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

    emit_scheduler_state_struct(out, program, delivery);
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
    out << "    return reserve_entity(registry);\n";
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
    // a delay unchanged. Delivery constrains consumer cardinality by
    // this recipient; it is never lowered to a validity guard around
    // broadcast dispatch.
    out << "template <typename Occurrence>\n";
    out << "void generated_emit_targeted_event(Occurrence occurrence, entt::entity target) {\n";
    out << "    emit_targeted_event(generated_scheduler_state().activation, std::move(occurrence), target);\n";
    out << "}\n\n";
    emit_delayed_emit_overloads(out, program, delivery, all_events);

    out << "void generated_drain_event_cascade(entt::registry& registry);\n\n";

    if (!lifecycle_triggers.empty() && delivery.cyclic()) {
        emit_lifecycle_routes(out, program, delivery);
    }
    out << "void generated_commit_activation(entt::registry& registry) {\n";
    if (!accumulation.empty()) {
        out << "    ::generated_flush_accumulations();\n";
    }
    out << "    auto& activation = generated_scheduler_state().activation;\n";
    if (lifecycle_triggers.empty()) {
        out << "    commit_activation(activation, registry, &generated_drain_event_cascade);\n";
    } else if (delivery.cyclic()) {
        out << "    commit_activation(activation, registry, &generated_drain_event_cascade, "
               "generated_lifecycle_trackers(), GeneratedLifecycleRoutes{});\n";
    } else {
        out << "    commit_activation(activation, registry, &generated_drain_event_cascade, "
               "generated_lifecycle_trackers());\n";
    }
    out << "}\n\n";

    for (const auto* event : external_events) {
        emit_external_event_injector(out, *event, program);
    }

    out << "template <typename Occurrence>\n";
    out << "void generated_dispatch_event([[maybe_unused]] entt::registry& registry, [[maybe_unused]] const Occurrence& occurrence) {}\n\n";

    emit_stable_handler_table(out, program);

    // Emits the render phase's dispatch call. When the program links
    // std.camera.viewport, the dispatch runs once per active viewport —
    // bracketed by that viewport's scissor region and camera — so queued
    // mesh/sprite submissions and immediate-mode draws (e.g. 2D shapes) both
    // land in the right screen region using the right camera, mirroring the
    // legacy generated_render_project's per-viewport loop (see
    // CppEnttCodegen::generate()). Without std.camera.viewport, it's a plain
    // single call, relying on the runtime's static default camera.
    const auto emit_render_dispatch = [&](const std::string& indent) {
        emit_render_phase_dispatch(out, program, render_phase, indent);
    };

    const auto phase_order = phase_activation_order(program);
    for (const auto* phase : phase_order) {
        emit_phase_batch(out, program, delivery, phase, render_phase, emit_render_dispatch);
    }

    // The per-event generated_dispatch_event overloads are defined later in
    // the file but called by the root-event processors below.
    for (const auto& type : occurrence_types) {
        out << "void generated_dispatch_event(entt::registry& registry, const " << type
            << "& occurrence, std::optional<entt::entity> target = std::nullopt);\n";
    }
    out << "\n";

    // An occurrence no phase claims as its root (e.g. std.persistence's save
    // outcomes, or a delayed occurrence of the host clock) is still its own
    // activation: dispatch its handlers, drain whatever cascade they queue,
    // then commit, exactly like a phase batch does.
    const auto emit_own_activation = [&out, &delivery] {
        out << "    if (target.has_value() && !registry.valid(*target)) {\n";
        out << "        return;\n";
        out << "    }\n";
        out << "    generated_scheduler_state().activation.active = true;\n";
        if (delivery.cyclic()) {
            out << "    generated_scheduler_state().activation.current_delayed = "
                   "&generated_scheduler_state().delayed_host;\n";
        }
        out << "    generated_dispatch_event(registry, root_event, target);\n";
        out << "    generated_drain_event_cascade(registry);\n";
        out << "    generated_commit_activation(registry);\n";
        out << "    generated_scheduler_state().activation.active = false;\n";
    };

    // Every event gets a processor, not only external ones: a host-clock
    // delayed occurrence of any event runs as its own activation.
    for (const auto* event : all_events) {
        emit_root_event_processor(out, program, event, frame_event, render_phase, phase_order, emit_own_activation);
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

    emit_host_clock_entry_points(out, delivery, program);
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

bool emit_phase_handler_call(std::ostringstream& out,
                             const DecoratedProgram& program,
                             const auto& phase,
                             const auto& identity,
                             std::unordered_set<std::string>& emitted_callees) {
    if (identity.trigger.kind != HandlerTriggerKind::Phase || identity.trigger.symbol != phase.phase) {
        return false;
    }
    const auto graph_node = std::ranges::find_if(program.execution_graph.handlers,
                                                 [&](const auto& node) { return node.identity == identity; });
    if (graph_node == program.execution_graph.handlers.end()) {
        return false;
    }
    if (is_user_external_handler(program, *graph_node)) {
        if (emitted_callees.insert(make_canonical_id(graph_node->identity.rule)).second) {
            emit_external_handler_call(out, program, *graph_node, "phase", "    ");
        }
        return true;
    }
    if (graph_node->implementation == HandlerImplementationKind::External) {
        const auto* rule = find_external_rule(program, graph_node->identity.rule);
        if (rule == nullptr || !rule->is_stdlib) {
            return false;
        }
        const auto callee = system_function_name(program.module_name, rule->name, "tick").ref();
        if (emitted_callees.insert(callee).second) {
            out << "    " << callee << "(registry);\n";
        }
        return true;
    }
    if (graph_node->implementation != HandlerImplementationKind::Cactus) {
        return false;
    }
    const auto callee = cactus_handler_callee(program, identity);
    if (!callee.has_value()) {
        return false;
    }
    if (emitted_callees.insert(*callee).second) {
        out << "    " << *callee << "(registry, phase);\n";
    }
    return true;
}

struct DispatchCall {
    HandlerIdentity identity;
    std::string text;
};

// A set of two or more calls runs, under CACTUS_LINEARIZATION_HARNESS, in a
// seeded linear extension of the schedule edges inside the set.
// In a program with a delivery cycle each call first records its handler's
// cycle; with `routed`, a call is skipped when the occurrence's route excludes it.
void emit_dispatch_calls(std::ostringstream& out,
                         const DecoratedProgram& program,
                         const AccumulationPlan& accumulation,
                         const DeliveryPlan& delivery,
                         const std::vector<DispatchCall>& calls,
                         bool routed = false) {
    const auto text = [&](const DispatchCall& call) {
        auto body = accumulation.entry_flushes(call.identity) + call.text + accumulation.exit_rebase(call.identity);
        if (!delivery.cyclic()) {
            return body;
        }
        const auto component = delivery.component_literal(call.identity);
        body                 = "    activation.current_component = " + component + ";\n" + body;
        if (!routed) {
            return body;
        }
        return "    if (route_admits(activation.delivery_route, activation.delivery_component, " + component + ")) {\n" +
               body + "    }\n";
    };
    if (delivery.cyclic() && !calls.empty()) {
        out << "    auto& activation = generated_scheduler_state().activation;\n";
    }
    if (calls.size() < 2) {
        for (const auto& call : calls) {
            out << text(call);
        }
        return;
    }
    const auto slot_of = [&calls](const HandlerIdentity& identity) {
        return std::ranges::find(calls, identity, &DispatchCall::identity);
    };
    std::size_t edge_count = 0;
    std::string edges;
    for (const auto& edge : program.execution_graph.schedule_edges) {
        const auto before = slot_of(edge.before);
        const auto after  = slot_of(edge.after);
        if (before == calls.end() || after == calls.end() || before == after) {
            continue;
        }
        edges += std::string(edge_count++ == 0 ? "" : ", ") + "{" + std::to_string(before - calls.begin()) + ", " +
                 std::to_string(after - calls.begin()) + "}";
    }
    out << "#ifdef CACTUS_LINEARIZATION_HARNESS\n";
    out << "    static constexpr std::array<cactus::runtime::entt_backend::LinearizationEdge, " << edge_count
        << "> kLinearizationEdges{" << (edges.empty() ? "" : "{" + edges + "}") << "};\n";
    out << "    cactus::runtime::entt_backend::dispatch_linear_extension(" << calls.size()
        << ", kLinearizationEdges, [&](std::size_t cactus_index) {\n";
    out << "        switch (cactus_index) {\n";
    for (std::size_t index = 0; index < calls.size(); ++index) {
        out << "            case " << index << ":\n";
        out << text(calls[index]);
        out << "                break;\n";
    }
    out << "            default:\n";
    out << "                break;\n";
    out << "        }\n";
    out << "    });\n";
    out << "#else  // CACTUS_LINEARIZATION_HARNESS\n";
    for (const auto& call : calls) {
        out << text(call);
    }
    out << "#endif  // CACTUS_LINEARIZATION_HARNESS\n";
}

bool emit_phase_handler_calls(std::ostringstream& out,
                              const DecoratedProgram& program,
                              const AccumulationPlan& accumulation,
                              const DeliveryPlan& delivery,
                              const auto& phase) {
    bool emitted = false;
    std::unordered_set<std::string> emitted_callees;
    std::vector<DispatchCall> calls;
    for (const auto& identity : program.execution_graph.stable_topological_order) {
        std::ostringstream call;
        emitted |= emit_phase_handler_call(call, program, phase, identity, emitted_callees);
        if (!call.view().empty()) {
            calls.push_back(DispatchCall{.identity = identity, .text = std::move(call).str()});
        }
    }
    emit_dispatch_calls(out, program, accumulation, delivery, calls);
    return emitted;
}

void emit_phase_handler_dispatch(std::ostringstream& out,
                                 const DecoratedProgram& program,
                                 const AccumulationPlan& accumulation,
                                 const DeliveryPlan& delivery,
                                 const auto& phase) {
    out << "void generated_dispatch_phase(entt::registry& registry, const " << cpp_type_ref(phase.phase)
        << "& phase) {\n";
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
        return;
    }

    const bool emitted = emit_phase_handler_calls(out, program, accumulation, delivery, phase);
    if (!emitted) {
        out << "    (void)registry;\n";
        out << "    (void)phase;\n";
    }
    out << "}\n\n";
}

// The runtime queue a native consumer's occurrences join.
std::string_view native_consumer_queue(std::string_view event) {
    if (event == "std.persistence.SaveRequested") {
        return "generated_queue_save_request";
    }
    if (event == "std.persistence.RestoreRequested") {
        return "generated_queue_restore_request";
    }
    throw std::logic_error("cpp-entt backend: no runtime queue for native consumer '" + std::string(event) + "'");
}

void emit_event_handler_dispatch(std::ostringstream& out,
                                 const DecoratedProgram& program,
                                 const AccumulationPlan& accumulation,
                                 const DeliveryPlan& delivery,
                                 const ResolvedEvent* event) {
    const auto event_type = event_runtime_cpp_type(program, *event->symbol_id);
    // The default argument lives on the forward declaration emitted by
    // emit_graph_scheduler_state; repeating it here would be a redefinition.
    out << "void generated_dispatch_event(entt::registry& registry, const " << event_type << "& occurrence, "
        << "std::optional<entt::entity> target) {\n";
    out << "    (void)occurrence;\n";
    out << "    (void)target;\n";
    // A runtime-consumed event joins its runtime queue whether or not any
    // rule also declares a handler for it.
    if (event->symbol_id.has_value()) {
        const auto canonical = make_canonical_id(*event->symbol_id);
        if (std::ranges::contains(native_consumers(), canonical, &NativeConsumer::event)) {
            out << "    " << native_consumer_queue(canonical) << "(occurrence.slot, occurrence.request_id);\n";
        }
    }
    std::vector<DispatchCall> calls;
    for (const auto& identity : program.execution_graph.stable_topological_order) {
        if (identity.trigger.kind != HandlerTriggerKind::Event || identity.trigger.symbol != *event->symbol_id) {
            continue;
        }
        const auto graph_node = std::ranges::find_if(program.execution_graph.handlers,
                                                     [&](const auto& node) { return node.identity == identity; });
        if (graph_node == program.execution_graph.handlers.end()) {
            continue;
        }
        std::ostringstream call;
        if (is_user_external_handler(program, *graph_node)) {
            emit_external_handler_call(call, program, *graph_node, "occurrence", "    ", "target");
            calls.push_back(DispatchCall{.identity = identity, .text = std::move(call).str()});
            continue;
        }
        if (graph_node->implementation == HandlerImplementationKind::External) {
            const auto* rule = find_external_rule(program, graph_node->identity.rule);
            if (rule != nullptr && rule->is_stdlib) {
                // Event-triggered extern rules (editor-debug-draw / editor-screen-ui generic
                // renderers) have no filter view to iterate, so their generated body takes the
                // occurrence directly instead of the (registry)-only shape phase-triggered
                // filter/view extern rules use — see emit_event_renderer_body in system_emitter.cpp.
                call << "    " << system_function_name(program.module_name, rule->name, "tick").ref()
                     << "(registry, occurrence);\n";
                calls.push_back(DispatchCall{.identity = identity, .text = std::move(call).str()});
            }
            continue;
        }
        if (graph_node->implementation != HandlerImplementationKind::Cactus) {
            continue;
        }
        if (const auto callee = cactus_handler_callee(program, identity)) {
            call << "    " << *callee << "(registry, occurrence, target);\n";
            calls.push_back(DispatchCall{.identity = identity, .text = std::move(call).str()});
        }
    }
    const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Event, .symbol = *event->symbol_id};
    emit_dispatch_calls(out, program, accumulation, delivery, calls, delivery.delayed(trigger) != nullptr);
    if (calls.empty()) {
        out << "    (void)registry;\n";
        out << "    (void)occurrence;\n";
    }
    out << "}\n\n";
}

std::string emit_graph_handler_dispatch(const DecoratedProgram& program,
                                        const AccumulationPlan& accumulation,
                                        const DeliveryPlan& delivery) {
    if (program.execution_graph.phases.empty() || program.ast == nullptr) {
        return {};
    }

    std::ostringstream out;
    out << "namespace cactus::runtime::entt_backend {\n\n";
    for (const auto& phase : program.execution_graph.phases) {
        emit_phase_handler_dispatch(out, program, accumulation, delivery, phase);
    }

    const auto events = sorted_events(program);
    for (const auto* event : events) {
        emit_event_handler_dispatch(out, program, accumulation, delivery, event);
    }

    // Lifecycle triggers are only ever declared on Cactus rules.
    for (const auto& trigger : lifecycle_occurrence_triggers(program)) {
        out << "void generated_dispatch_event(entt::registry& registry, const "
            << *EnttCodegenUtils::lifecycle_occurrence_cpp_type(trigger, program)
            << "& occurrence, std::optional<entt::entity> target) {\n";
        std::vector<DispatchCall> calls;
        for (const auto& identity : program.execution_graph.stable_topological_order) {
            if (identity.trigger != trigger) {
                continue;
            }
            if (const auto callee = cactus_handler_callee(program, identity)) {
                calls.push_back(
                    DispatchCall{.identity = identity, .text = "    " + *callee + "(registry, occurrence, target);\n"});
            }
        }
        emit_dispatch_calls(out, program, accumulation, delivery, calls, delivery.delayed(trigger) != nullptr);
        if (calls.empty()) {
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
    if (!accumulation.empty()) {
        out << "    ::generated_flush_accumulations();\n";
    }
    out << "}\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

std::string emit_projected_trait_registry_helpers(const DecoratedProgram& program) {
    std::ostringstream out;
    out << "// ── Projected Trait Registry Tracking ────────────────────────────────\n\n";
    out << "namespace cactus_gen::detail {\n\n";
    // Tracking storage and logic are runtime-hosted (ProjectedTraitTracker); one tracker per trait type.
    out << "template <class T>\n";
    out << "cactus::runtime::entt_backend::ProjectedTraitTracker<T> projected;  // "
           "NOLINT(cppcoreguidelines-avoid-non-const-global-variables)\n\n";
    out << "template <class T>\n";
    out << "[[maybe_unused]] decltype(auto) project(entt::registry& registry, entt::entity entity) {\n";
    out << "    return projected<T>.project(registry, entity);\n";
    out << "}\n\n";
    out << "template <class T>\n";
    out << "[[maybe_unused]] T* durable(entt::registry& registry, entt::entity entity) {\n";
    out << "    return projected<T>.durable_value(registry, entity);\n";
    out << "}\n\n";
    out << "template <class T>\n";
    out << "[[maybe_unused]] void cancel_projected(entt::entity entity) {\n";
    out << "    projected<T>.cancel(entity);\n";
    out << "}\n\n";
    out << "void clear_projected_traits(entt::registry& registry) {\n";
    for (const auto& [name, trait] : program.traits) {
        out << "    projected<" << cpp_type_ref(SymbolKind::Trait, trait.module_name, trait.name)
            << ">.clear(registry);\n";
    }
    out << "}\n\n";
    out << "}  // namespace cactus_gen::detail\n\n";
    return out.str();
}

// A view constant copies another view, not the `std::string` a read of it produces.
std::string constant_initializer_cpp(const ExprNode& value, bool view, const DecoratedProgram& program) {
    const auto* alias = view ? EnttCodegenUtils::named_const(value) : nullptr;
    return alias != nullptr ? EnttCodegenUtils::symbol_cpp_name(*alias) : EnttCodegenUtils::emit_expr(value, program);
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
            const auto name = EnttCodegenUtils::phase_dt_name(phase.phase);
            out << namespace_block(name.ns,
                                   "inline constexpr float " + name.name + " = static_cast<float>(" +
                                       cpp_double_literal(*phase.every_seconds) + ");\n");
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
        const bool compile_time = EnttCodegenUtils::is_constexpr_const(program, symbol);
        const bool view         = compile_time && constant->type.kind == TypeKind::String;
        const auto type         = view ? std::string("std::string_view") : EnttCodegenUtils::type_to_cpp(constant->type);
        out << namespace_block(cpp_namespace_path(symbol.module),
                               std::string(compile_time ? "inline constexpr " : "inline const ") + type + " " +
                                   cpp_decl_name(symbol) + " = " + constant_initializer_cpp(*value->second, view, program) +
                                   ";\n");
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
    out << "    const auto* b_body = registry.try_get<" << body_cpp << ">(b);\n";
    out << "    const bool both_bodies = registry.all_of<" << body_cpp
        << ">(a) && b_body != nullptr && !b_body->paused;\n";
    out << "    return " << ns << "push_out(a_shape, b_shape, a == b, both_bodies);\n";
    out << "}\n\n";
    return out.str();
}

// The root module is linked last, so its window constant wins over an imported one.
std::optional<SymbolId> window_constant_symbol(const DecoratedProgram& program, const std::string& name) {
    const auto found = std::ranges::find_if(program.const_order | std::views::reverse,
                                            [&](const SymbolId& symbol) { return symbol.local_name == name; });
    if (found == (program.const_order | std::views::reverse).end()) {
        return std::nullopt;
    }
    return *found;
}

std::optional<std::string> window_constant(const DecoratedProgram& program, const std::string& name) {
    return window_constant_symbol(program, name).transform(&EnttCodegenUtils::symbol_cpp_name);
}

// A view title is spelled as the literal it views: `.data()` of a view is not known to be null-terminated.
std::string window_title(const DecoratedProgram& program) {
    const auto symbol = window_constant_symbol(program, "WINDOW_TITLE");
    if (!symbol.has_value()) {
        return "\"Cactus Game\"";
    }
    if (!EnttCodegenUtils::is_constexpr_const(program, *symbol)) {
        return EnttCodegenUtils::symbol_cpp_name(*symbol) + ".c_str()";
    }
    const auto* value = EnttCodegenUtils::find_const_initializer(*program.ast, *symbol);
    while (!std::holds_alternative<LiteralExpr>(value->expr)) {
        value = EnttCodegenUtils::find_const_initializer(*program.ast, *EnttCodegenUtils::named_const(*value));
    }
    return EnttCodegenUtils::emit_expr(*value, program);
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

// Each button's `prop` binding as a raylib constant, in declaration order; `fallback` when unbound.
std::vector<std::string> button_bindings(const DecoratedProgram& program,
                                         const std::string& prop_key,
                                         const std::string& fallback,
                                         const auto& to_raylib) {
    std::vector<std::string> bindings;
    for (const auto& decl : program.ast->declarations) {
        const auto* input = std::get_if<InputDeclNode>(&decl);
        if (input == nullptr || input->input_kind != InputKind::Button) {
            continue;
        }
        std::string binding = fallback;
        for (const auto& prop : input->props) {
            if (prop.key != prop_key) {
                continue;
            }
            const auto constant = to_raylib(*prop.value);
            if (!constant.has_value()) {
                fail_unresolved_input_binding(input->name, prop.key);
            }
            binding = *constant;
        }
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

// One case-label list per distinct binding; buttons bound to the fallback share the default.
void emit_button_binding_switch(std::ostringstream& out,
                                const DecoratedProgram& program,
                                const std::string& name,
                                const std::string& prop_key,
                                const std::string& fallback,
                                const auto& to_raylib) {
    const auto bindings = button_bindings(program, prop_key, fallback, to_raylib);
    std::vector<std::string> distinct;
    for (const auto& binding : bindings) {
        if (binding != fallback && std::ranges::find(distinct, binding) == distinct.end()) {
            distinct.push_back(binding);
        }
    }
    if (distinct.empty()) {
        out << "int " << name << "([[maybe_unused]] std::uint8_t button) noexcept {\n";
        out << "    return " << fallback << ";\n";
        out << "}\n";
        return;
    }
    out << "int " << name << "(std::uint8_t button) noexcept {\n";
    out << "    switch (button) {\n";
    for (const auto& value : distinct) {
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            if (bindings[index] == value) {
                out << "        case static_cast<::cactus_gen::detail::InputButton>(" << index << "):\n";
            }
        }
        out << "            return " << value << ";\n";
    }
    out << "        default:\n";
    out << "            break;\n";
    out << "    }\n";
    out << "    return " << fallback << ";\n";
    out << "}\n";
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
        out << "namespace cactus_gen::detail {\n";
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
        out << "}  // namespace cactus_gen::detail\n\n";

        out << "namespace cactus::runtime::entt_backend {\n";
        emit_button_binding_switch(out, program, "cactus_input_button_key", "key", "0", raylib_key_constant);
        emit_button_binding_switch(out, program, "cactus_input_button_mouse", "mouse", "-1", raylib_mouse_constant);
        out << "}  // namespace cactus::runtime::entt_backend\n\n";
    }

    if (has_axis_input) {
        out << "namespace cactus_gen::detail {\n";
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
        out << "}  // namespace cactus_gen::detail\n\n";

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
                out << "        case static_cast<::cactus_gen::detail::InputAxis>(::cactus_gen::detail::InputAxisTag::"
                    << input->name << "):\n";
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
        out << "namespace cactus_gen::detail {\n";
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
        out << "};\n";
        out << "}  // namespace cactus_gen::detail\n\n";
    }

    return out.str();
}

// Emits the cactus_gen_translate_camera_2d/3d helpers (raylib-facing camera struct
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
        out << "Camera2D cactus_gen_translate_camera_2d(const " << cam2d_cpp << "& cam, int sw, int sh) noexcept {\n";
        out << "    Camera2D cam2d{};\n";
        out << "    cam2d.target   = cam.offset;\n";
        out << "    cam2d.zoom     = cam.zoom;\n";
        out << "    cam2d.rotation = cam.rotation * (180.0F / std::numbers::pi_v<float>);\n";
        out << "    cam2d.offset   = {.x = static_cast<float>(sw) * 0.5F,\n";
        out << "                      .y = static_cast<float>(sh) * 0.5F};\n";
        out << "    return cam2d;\n";
        out << "}\n";
    }
    if (emit_3d_helper) {
        out << "Camera3D cactus_gen_translate_camera_3d(entt::entity entity, const " << cam3d_cpp
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

std::string emit_accumulation_tables(const AccumulationPlan& accumulation, const DecoratedProgram& program) {
    if (accumulation.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "// ── Exact Float Accumulation ───────────────────────────────────────\n\n";
    // One specialization per trait, one table per buffered field.
    std::vector<std::pair<std::string, std::vector<std::string>>> tables_by_trait;
    for (const auto& field : accumulation.fields()) {
        const auto trait = EnttCodegenUtils::trait_cpp_name(field.trait, field.trait.local_name, program);
        auto found       = std::ranges::find(tables_by_trait, trait, &decltype(tables_by_trait)::value_type::first);
        if (found == tables_by_trait.end()) {
            found = tables_by_trait.insert(tables_by_trait.end(), {trait, {}});
        }
        found->second.push_back(cpp_escaped(*field.field));
    }
    std::ostringstream tables;
    tables << "template <class T>\n";
    tables << "struct accumulation;\n\n";
    for (const auto& [trait, fields] : tables_by_trait) {
        tables << "template <>\n";
        tables << "struct accumulation<" << trait << "> {\n";
        for (const auto& field : fields) {
            tables << "    static cactus::runtime::AccumulatedDeltas<entt::entity>& " << field << "() {\n";
            tables << "        static cactus::runtime::AccumulatedDeltas<entt::entity> deltas;\n";
            tables << "        return deltas;\n";
            tables << "    }\n";
        }
        tables << "};\n\n";
    }
    out << namespace_block("cactus_gen::detail", tables.str()) << "\n";
    out << "inline void generated_flush_accumulations() {\n";
    for (const auto& table : accumulation.tables()) {
        out << "    " << table << "().flush();\n";
    }
    out << "}\n\n";
    out << "[[maybe_unused]] inline void generated_rebase_accumulations(entt::registry& registry) {\n";
    for (std::size_t index = 0; index < accumulation.tables().size(); ++index) {
        const auto& field = accumulation.fields()[index];
        const auto trait  = EnttCodegenUtils::trait_cpp_name(field.trait, field.trait.local_name, program);
        out << "    " << accumulation.tables()[index]
            << "().rebase([&](entt::entity entity, std::uint8_t component) -> float* {\n";
        out << "        auto* const trait = registry.valid(entity) ? registry.try_get<" << trait
            << ">(entity) : nullptr;\n";
        out << "        return trait == nullptr ? nullptr : cactus::runtime::accumulation_cell(trait->" << *field.field
            << ", component);\n";
        out << "    });\n";
    }
    out << "}\n\n";
    return out.str();
}

// Named-slot members nest by module path, in declaration order.
struct NamedSlotTree {
    std::vector<std::pair<std::string, NamedSlotTree>> children;

    NamedSlotTree& child(const std::string& name) {
        const auto found = std::ranges::find(children, name, &std::pair<std::string, NamedSlotTree>::first);
        if (found != children.end()) {
            return found->second;
        }
        return children.emplace_back(name, NamedSlotTree{}).second;
    }
};

void emit_named_slot_members(std::ostringstream& out, const NamedSlotTree& tree, const std::string& indent) {
    for (const auto& [name, node] : tree.children) {
        if (node.children.empty()) {
            out << indent << "entt::entity " << name << " = entt::null;\n";
            continue;
        }
        out << indent << "struct {\n";
        emit_named_slot_members(out, node, indent + "    ");
        out << indent << "} " << name << ";\n";
    }
}

// A slot follows the restored record whose archetype origin is its declaration,
// and is stale when the document has none.
std::string emit_named_slots(const DecoratedProgram& program) {
    const auto entities = EnttCodegenUtils::declared_entities(program);
    if (entities.empty()) {
        return {};
    }
    std::ostringstream out;
    NamedSlotTree tree;
    for (const auto* entity : entities) {
        auto* node = &tree;
        for (const auto& segment : EnttCodegenUtils::named_slot_path(entity_symbol(*entity, program))) {
            node = &node->child(segment);
        }
    }
    out << "struct GeneratedNamedSlots {\n";
    emit_named_slot_members(out, tree, "    ");
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
            origins += (origins.empty() ? "{.node = " : ", {.node = ") + std::to_string(*origin) + ", .slot = &" +
                       EnttCodegenUtils::named_slot_name(symbol) + "}";
        } else {
            out << "    " << EnttCodegenUtils::named_slot_name(symbol) << " = entt::null;\n";
        }
    }
    if (!origins.empty()) {
        out << "    cactus::runtime::entt_backend::rebind_named_slots(registry, {" << origins << "});\n";
    }
    out << "}\n";
    return "// ── Named Entities ─────────────────────────────────────────────────\n\n" +
           namespace_block("cactus_gen::detail", out.str()) + "\n";
}

// `auto component = T{};` plus the archetype's assignments: the value a creation site gives `trait`.
void emit_component_value(std::ostringstream& out,
                          const ArchetypeTraitEntry& trait,
                          const std::string& cpp_name,
                          const DecoratedProgram& program,
                          const std::string& ind) {
    std::size_t widest = std::string("auto component").size();
    for (const auto& assignment : trait.assignments) {
        widest = std::max(widest, std::string("component.").size() + assignment.name.size());
    }
    out << ind << pad_to_width("auto component", widest) << " = " << cpp_name << "{};\n";
    for (const auto& assignment : trait.assignments) {
        out << ind << pad_to_width("component." + cpp_escaped(assignment.name), widest) << " = "
            << EnttCodegenUtils::emit_expr(*assignment.value, program) << ";\n";
    }
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
    // independently emplace CreationOrdinal at their own create_entity()
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
            out << ind;
            if (variant.has_value()) {
                out << EnttCodegenUtils::enter_state_call(*variant, entity_name, cpp_name + "{}");
            } else {
                out << "registry.emplace<" << cpp_name << ">(" << entity_name << ");\n";
            }
            continue;
        }

        const bool retain = origin.has_value() && EnttPersistenceEmitter::trait_has_construction_payload(
                                                      program, trait.resolved_trait_id, trait.trait_name);
        out << ind << "{\n";
        emit_component_value(out, trait, cpp_name, program, ind + "    ");
        out << ind << "    ";
        if (variant.has_value()) {
            out << EnttCodegenUtils::enter_state_call(*variant, entity_name, "component");
        } else {
            out << "registry.emplace<" << cpp_name << ">(" << entity_name << ", component);\n";
        }
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
    out << "entt::entity " << at_name.name << "(entt::registry& registry, entt::entity hint) {\n";
    out << "    auto entity = cactus::runtime::entt_backend::adopt_or_create(registry, hint);\n";
    emit_archetype_trait_initializers(
        out, traits, program, "entity", 1, EnttPersistenceEmitter::archetype_origin_index(program, archetype));
    out << "    return entity;\n";
    out << "}\n\n";
    // Delegates rather than re-running emit_archetype_trait_initializers: the
    // hinted function above already performs every field initialization; the
    // only thing this one adds is an arbitrary (non-hinted) entity.
    out << "entt::entity " << create_name.name << "(entt::registry& registry) {\n";
    out << "    return " << at_name.name << "(registry, cactus::runtime::entt_backend::create_entity(registry));\n";
    out << "}\n\n";
    return namespace_block(create_name.ns, out.str()) + "\n";
}

// ── Hierarchical archetype creation (dsl-hierarchical-entity-templates) ──────

void emit_archetype_node_helpers(std::ostringstream& out,
                                 const std::string& archetype_name,
                                 const SymbolId& archetype,
                                 const std::vector<ChildArchetypeNode>& children,
                                 const DecoratedProgram& program,
                                 std::vector<std::string>& role_path,
                                 const std::string& parameters = {}) {
    for (const auto& child : children) {
        role_path.push_back(child.role);
        const auto name = archetype_node_create_function_name(program.module_name, archetype_name, role_path);
        std::ostringstream body;
        body << "static entt::entity " << name.name << "(entt::registry& registry" << parameters << ") {\n";
        body << "    auto entity = cactus::runtime::entt_backend::create_entity(registry);\n";
        emit_archetype_trait_initializers(
            body,
            child.traits,
            program,
            "entity",
            1,
            EnttPersistenceEmitter::archetype_origin_index(program, archetype, role_path));
        body << "    return entity;\n";
        body << "}\n";
        out << namespace_block(name.ns, body.str()) << "\n";
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
            << archetype_node_create_function_name(module_name, archetype_name, role_path).ref() << "(registry"
            << arguments << ");\n";
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

// Slot values that own memory travel by const reference.
bool owns_memory(const TypeInfo& type) {
    return type.kind == TypeKind::String || type.kind == TypeKind::List || type.kind == TypeKind::Struct;
}

std::string slot_parameter_type(const TypeInfo& type) {
    const auto cpp = EnttCodegenUtils::value_type_to_cpp(type);
    return owns_memory(type) ? "const " + cpp + "&" : cpp;
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
        const auto declaration = ", [[maybe_unused]] " + slot_parameter_type(slot.type) + " " + initializer_slot_name(slot.index);
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
    std::ostringstream nodes;
    nodes << "static entt::entity " << node_at.name << "(entt::registry& registry, entt::entity hint" << all_parameters
          << ") {\n";
    nodes << "    auto entity = cactus::runtime::entt_backend::adopt_or_create(registry, hint);\n";
    emit_archetype_trait_initializers(
        nodes, traits, program, "entity", 1, EnttPersistenceEmitter::archetype_origin_index(program, archetype));
    nodes << "    return entity;\n}\n";
    nodes << "[[maybe_unused]] static entt::entity " << node_create.name << "(entt::registry& registry"
          << all_parameters << ") {\n";
    nodes << "    return " << node_at.name << "(registry, cactus::runtime::entt_backend::create_entity(registry)"
          << all_arguments << ");\n}\n";
    out << namespace_block(node_at.ns, nodes.str());
    emit_archetype_node_helpers(out, name, archetype, children, program, role_path, all_parameters);
    std::ostringstream creators;
    creators << "entt::entity " << create_at.name << "(entt::registry& registry, entt::entity hint" << parameters
             << ") {\n";
    for (const auto& slot : slots) {
        if (slot.value != nullptr) {
            creators << "    [[maybe_unused]] const " << EnttCodegenUtils::value_type_to_cpp(slot.type)
                     << (owns_memory(slot.type) ? "& " : " ") << initializer_slot_name(slot.index) << " = "
                     << EnttCodegenUtils::emit_expr(*slot.value, program) << ";\n";
        }
    }
    creators << "    auto entity = " << node_at.ref() << "(registry, hint" << all_arguments << ");\n";
    emit_child_creation_sequence(
        creators, program.module_name, name, children, "entity", "child", role_path, program, all_arguments);
    creators << "    return entity;\n}\n";
    creators << "entt::entity " << create.name << "(entt::registry& registry" << parameters << ") {\n";
    creators << "    return " << create_at.name << "(registry, cactus::runtime::entt_backend::create_entity(registry)"
             << arguments << ");\n}\n";
    out << namespace_block(create.ns, creators.str()) << "\n";
    return out.str();
}

struct RangedNodeTrait {
    std::vector<std::string> role_path;
    const ArchetypeTraitEntry* entry = nullptr;
    const ResolvedTrait* trait       = nullptr;
};

// NOLINTNEXTLINE(misc-no-recursion) -- walks the archetype's child tree
void collect_ranged_node_traits(const std::vector<ArchetypeTraitEntry>& traits,
                                const std::vector<ChildArchetypeNode>& children,
                                const DecoratedProgram& program,
                                std::vector<std::string>& role_path,
                                std::vector<RangedNodeTrait>& found) {
    for (const auto& entry : traits) {
        if (const auto* trait = find_ranged_trait(program, entry.resolved_trait_id, entry.trait_name)) {
            found.push_back(RangedNodeTrait{.role_path = role_path, .entry = &entry, .trait = trait});
        }
    }
    for (const auto& child : children) {
        role_path.push_back(child.role);
        collect_ranged_node_traits(child.traits, child.children, program, role_path, found);
        role_path.pop_back();
    }
}

bool archetype_has_ranged_traits(const std::vector<ArchetypeTraitEntry>& traits,
                                 const std::vector<ChildArchetypeNode>& children,
                                 const DecoratedProgram& program) {
    std::vector<RangedNodeTrait> ranged;
    std::vector<std::string> role_path;
    collect_ranged_node_traits(traits, children, program, role_path, ranged);
    return !ranged.empty();
}

// The value each node of the archetype gives each of its ranged traits, and a
// check of all of them, so a spawn or load can refuse before creating anything.
// Each takes the creation function's slot parameters.
std::string emit_range_value_functions(const std::string& archetype_name,
                                       const std::vector<ArchetypeTraitEntry>& traits,
                                       const std::vector<ChildArchetypeNode>& children,
                                       const std::vector<InitializerSlot>& slots,
                                       const DecoratedProgram& program) {
    std::vector<RangedNodeTrait> ranged;
    std::vector<std::string> role_path;
    collect_ranged_node_traits(traits, children, program, role_path, ranged);
    if (ranged.empty()) {
        return {};
    }
    std::vector<std::string> parameters;
    std::vector<std::string> arguments;
    std::ostringstream bound_slots;
    for (const auto& slot : slots) {
        if (slot.value != nullptr) {
            bound_slots << "    [[maybe_unused]] const " << EnttCodegenUtils::value_type_to_cpp(slot.type)
                        << (owns_memory(slot.type) ? "& " : " ") << initializer_slot_name(slot.index) << " = "
                        << EnttCodegenUtils::emit_expr(*slot.value, program) << ";\n";
            continue;
        }
        parameters.push_back("[[maybe_unused]] " + slot_parameter_type(slot.type) + " " +
                             initializer_slot_name(slot.index));
        arguments.push_back(initializer_slot_name(slot.index));
    }
    const auto parameter_list = join_strings(parameters, ", ");
    const auto call_arguments = "(" + join_strings(arguments, ", ") + ")";
    const auto precheck = archetype_range_precheck_function_name(program.module_name, archetype_name);
    std::ostringstream out;
    std::vector<std::string> checks;
    for (const auto& node : ranged) {
        const auto name = range_value_function_name(program.module_name, archetype_name, node.role_path, node.trait->name);
        const auto cpp  = EnttCodegenUtils::trait_cpp_name(node.entry->resolved_trait_id, node.entry->trait_name, program);
        out << "[[maybe_unused]] static " << cpp << " " << name.name << "(" << parameter_list << ") {\n";
        out << bound_slots.str();
        emit_component_value(out, *node.entry, cpp, program, "    ");
        out << "    return component;\n}\n";
        checks.push_back(emit_range_check_passes(*node.trait, name.name + call_arguments));
    }
    out << "[[maybe_unused]] static bool " << precheck.name << "([[maybe_unused]] entt::registry& registry"
        << (parameter_list.empty() ? "" : ", " + parameter_list) << ") {\n";
    out << "    return " << join_strings(checks, " && ") << ";\n}\n";
    return namespace_block(precheck.ns, out.str()) + "\n";
}

std::string emit_archetype_creation_functions_unchecked(const std::string& archetype_name,
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
    std::ostringstream nodes;
    nodes << "static entt::entity " << node_at_name.name << "(entt::registry& registry, entt::entity hint) {\n";
    nodes << "    auto entity = cactus::runtime::entt_backend::adopt_or_create(registry, hint);\n";
    emit_archetype_trait_initializers(
        nodes, traits, program, "entity", 1, EnttPersistenceEmitter::archetype_origin_index(program, archetype));
    nodes << "    return entity;\n";
    nodes << "}\n\n";
    // Delegates rather than re-running emit_archetype_trait_initializers, same
    // as the flat-archetype path in emit_archetype_creation_function.
    nodes << "static entt::entity " << node_name.name << "(entt::registry& registry) {\n";
    nodes << "    return " << node_at_name.name
          << "(registry, cactus::runtime::entt_backend::create_entity(registry));\n";
    nodes << "}\n\n";
    out << namespace_block(node_name.ns, nodes.str()) << "\n";
    emit_archetype_node_helpers(out, archetype_name, archetype, children, program, role_path);

    const auto create    = archetype_create_function_name(program.module_name, archetype_name);
    const auto create_at = archetype_create_at_function_name(program.module_name, archetype_name);
    std::ostringstream creators;
    creators << "entt::entity " << create.name << "(entt::registry& registry) {\n";
    creators << "    auto entity = " << node_name.ref() << "(registry);\n";
    emit_child_creation_sequence(
        creators, program.module_name, archetype_name, children, "entity", "child", role_path, program);
    creators << "    return entity;\n";
    creators << "}\n\n";

    creators << "entt::entity " << create_at.name << "(entt::registry& registry, entt::entity hint) {\n";
    creators << "    auto entity = " << node_at_name.ref() << "(registry, hint);\n";
    emit_child_creation_sequence(
        creators, program.module_name, archetype_name, children, "entity", "child", role_path, program);
    creators << "    return entity;\n";
    creators << "}\n";
    out << namespace_block(create.ns, creators.str()) << "\n";
    return out.str();
}

std::string emit_archetype_creation_functions(const std::string& archetype_name,
                                              const SymbolId& archetype,
                                              const std::vector<ArchetypeTraitEntry>& traits,
                                              const std::vector<ChildArchetypeNode>& children,
                                              const DecoratedProgram& program,
                                              const std::vector<InitializerSlot>& slots) {
    return emit_range_value_functions(archetype_name, traits, children, slots, program) +
           emit_archetype_creation_functions_unchecked(archetype_name, archetype, traits, children, program, slots);
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

// Whether a contact scoring `score` beats `best`; an exact tie goes to the older collider.
bool cactus_flat_beats(const entt::registry& registry,
                       const QueryContact2D& contact,
                       float score,
                       const std::optional<QueryContact2D>& best,
                       float best_score) {
    if (!best.has_value() || score > best_score) {
        return true;
    }
    return score == best_score && cactus::runtime::entt_backend::creation_ordinal(registry, contact.other) <
           cactus::runtime::entt_backend::creation_ordinal(registry, best->other);
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
        if (cactus_flat_beats(registry, *contact, -contact->distance, nearest, nearest ? -nearest->distance : 0.0F)) {
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
        if (cactus_flat_beats(registry, *contact, amount, deepest, deepest_amount)) {
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
    std::ranges::sort(contacts, {}, [&](const QueryContact2D& contact) {
        return cactus::runtime::entt_backend::creation_ordinal(registry, contact.other);
    });
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
              "inline void generated_process_persistence_boundary([[maybe_unused]] entt::registry& registry) {}\n\n"
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
        out << "    cactus::runtime::entt_backend::generated_run_boundary_activation(registry, "
            << event_runtime_cpp_type(program, *event.symbol_id) << "{});\n";
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
    out << "        if (cactus::runtime::entt_backend::quit_requested()) {\n";
    out << "            break;\n";
    out << "        }\n";
    out << "    }\n\n";
    if (has_unload_handler) {
        emit_boundary_activation(*unload_event);
    }
    out << "    cactus::runtime::entt_backend::release_render_resources();\n";
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
    // Before any system is emitted: it marks the statements that buffer.
    const AccumulationPlan accumulation(program);
    const AccumulationScope accumulation_scope(accumulation);
    const DeliveryPlan delivery(program);

    const bool uses_flat     = module_uses_camera_flat(program);
    const bool uses_viewport = module_uses_camera_viewport(program);
    const bool uses_volume   = module_uses_camera_volume(program);
    const bool uses_text     = uses_text_format(program);
    const bool uses_editor   = module_uses_editor(program);
    // Dimensionality from the root module's resolved WorldTransform references —
    // deterministic even when std.editor links both flat and volume variants.
    const WorldTransformUsage wt_usage = EnttCodegenUtils::world_transform_usage(program);
    // Camera-rig classification, computed once and reused everywhere it's
    // needed below (the early cactus_gen_translate_camera_2d/3d helper emission and
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
    out << "// Generated C++ mirrors authored DSL constants, declarations, and system control flow.\n\n";
    out << "#include \"common/float_mode.hpp\"\n\n";
    out << "#include \"backends/cpp-entt/runtime.hpp\"\n";
    out << "#ifdef CACTUS_LINEARIZATION_HARNESS\n";
    out << "#include \"backends/cpp-entt/linearization_harness.hpp\"\n";
    out << "#endif  // CACTUS_LINEARIZATION_HARNESS\n";
    if (program_uses_persistence(program)) {
        out << "#include \"backends/cpp-entt/persistence_file_adapter.hpp\"\n";
    }
    out << "\n";
    out << "#include <entt/entt.hpp>\n";
    out << "#include <raylib.h>\n";
    out << "\n";
    out << "\n";
    out << "#include <algorithm>\n";
    out << "#include <array>\n";
    out << "#include <cmath>\n";
    out << "#include <concepts>\n";
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
        out << "#include <tuple>\n";
        out << "#include <type_traits>\n";
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
                    << "U;\n";
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
        out << namespace_block(cpp_namespace_path("std.random"),
                               "using Rng        = cactus::runtime::stdlib::random::Rng;\n"
                               "using Uniform    = cactus::runtime::stdlib::random::Uniform;\n"
                               "using UniformInt = cactus::runtime::stdlib::random::UniformInt;\n"
                               "using Normal     = cactus::runtime::stdlib::random::Normal;\n");
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
    // A state slot declares its variants as nested types, so slots come first.
    for (const auto& [name, t] : program.traits) {
        if (t.is_state_slot()) {
            out << EnttComponentEmitter::emit_component(t, program) << "\n";
        }
    }
    for (const auto& [name, t] : program.traits) {
        if (t.is_state_slot() ||
            (uses_random && t.module_name == "std.random" && kRandomRuntimeTypes.contains(t.name))) {
            continue;
        }
        out << EnttComponentEmitter::emit_component(t, program) << "\n";
    }
    out << EnttComponentEmitter::emit_state_helpers(program);
    out << emit_world_digest(program);

    if (const auto collider_helpers = emit_collider_query_helpers(program); !collider_helpers.empty()) {
        out << namespace_block("cactus_gen::detail", collider_helpers) << "\n";
    }
    out << emit_named_slots(program);
    out << emit_accumulation_tables(accumulation, program);
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
    out << emit_range_check_functions(program);
    out << EnttRestoreEmitter::emit_world_restore(program);

    out << emit_event_structs(program);

    out << emit_external_command_forward_declarations(program);
    out << emit_graph_scheduler_state(program, accumulation, delivery);
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
    std::ostringstream persist_stubs;
    for (const auto& [name, t] : program.traits) {
        if (trait_declares_persist_field(t)) {
            const std::string cpp_name = cpp_type_ref(SymbolKind::Trait, t.module_name, t.name);
            persist_stubs << "void persist_save(const " << cpp_name << "& comp) {\n";
            persist_stubs << "    (void)comp;\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    persist_stubs << "    // serialize comp." << f.name << "\n";
                }
            }
            persist_stubs << "}\n\n";
            persist_stubs << "void persist_load(" << cpp_name << "& comp) {\n";
            persist_stubs << "    (void)comp;\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    persist_stubs << "    // deserialize into comp." << f.name << "\n";
                }
            }
            persist_stubs << "}\n\n";
        }
    }
    if (!persist_stubs.view().empty()) {
        out << namespace_block("cactus_gen::detail", persist_stubs.str()) << "\n";
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
        out << "namespace cactus_gen::detail {\n";
        out << "using TemplateFactory = entt::entity(*)(entt::registry&);\n";
        out << "struct TemplateEntry {\n";
        out << "    std::string_view name;\n";
        out << "    TemplateFactory create;\n";
        out << "};\n";
        // Declaration order is template_names()/template_index() order (editor-template-registry).
        out << "inline constexpr std::array<TemplateEntry, " << pub_template_count
            << "> cactus_template_registry = {{\n";
        for (const auto& decl : program.ast->declarations) {
            if (const auto* tmpl = std::get_if<TemplateNode>(&decl); tmpl != nullptr && tmpl->is_pub) {
                out << "    {.name = \"" << tmpl->name << "\", .create = &"
                    << archetype_create_function_name(program.module_name, tmpl->name).ref() << "},\n";
            }
        }
        out << "}};\n";
        out << "}  // namespace cactus_gen::detail\n\n";
        out << "namespace cactus::runtime::entt_backend {\n";
        // Not noexcept: returns std::vector<std::string> by value, which allocates and could
        // (in principle, on OOM) throw — clang-tidy's bugprone-exception-escape correctly flags
        // a noexcept function that isn't actually exception-safe.
        out << "std::vector<std::string> editor_template_names() {\n";
        out << "    std::vector<std::string> names;\n";
        out << "    names.reserve(::cactus_gen::detail::cactus_template_registry.size());\n";
        out << "    for (const auto& entry : ::cactus_gen::detail::cactus_template_registry) {\n";
        out << "        names.emplace_back(entry.name);\n";
        out << "    }\n";
        out << "    return names;\n";
        out << "}\n";
        out << "int editor_template_index(const std::string& name) noexcept {\n";
        out << "    const auto& order = ::cactus_gen::detail::cactus_template_registry;\n";
        out << "    for (std::size_t index = 0; index < order.size(); ++index) {\n";
        out << "        if (order[index].name == name) {\n";
        out << "            return static_cast<int>(index);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    return -1;\n";
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
        out << "            auto entity = cactus::runtime::entt_backend::create_entity(registry);\n";
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
                out << namespace_block(detail_namespace_path(program.module_name),
                                       EnttSystemEmitter::emit_system(*rule, program));
            }
            if (auto* rule = std::get_if<ExternRuleNode>(&decl); rule != nullptr) {
                if (has_compiler_owned_external_handler(program, *rule)) {
                    out << namespace_block(detail_namespace_path(program.module_name),
                                           EnttSystemEmitter::emit_extern_system(*rule, program));
                } else if (program.execution_graph.phases.empty() && has_user_external_handler(program, *rule)) {
                    throw std::runtime_error("cpp-entt user external rule '" +
                                             make_canonical_id(*rule->resolved_rule_id) +
                                             "' requires a linked phase graph and contract-shaped callback ABI");
                }
            }
        }
    }

    out << emit_graph_handler_dispatch(program, accumulation, delivery);

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
    const auto win_title  = window_title(program);
    const auto win_fps    = window_constant(program, "TARGET_FPS").value_or("60");

    out << "// ── Runtime Glue ────────────────────────────────────────────────────\n\n";
    out << "ProjectConfig generated_project_config() noexcept {\n";
    out << "    return {.window_width = " << win_width << ", .window_height = " << win_height
        << ", .window_title = " << win_title << ", .target_fps = " << win_fps << "};\n";
    out << "}\n\n";

    const bool tracks_lifecycle = EnttCodegenUtils::tracks_lifecycle(program);
    out << "void generated_init_project(entt::registry& registry) {\n";
    out << "    (void)registry;\n";
    out << "    cactus::runtime::entt_backend::reset_quit_request();\n";
    out << "    cactus::runtime::entt_backend::clear_runtime_faults();\n";
    out << "    cactus::runtime::entt_backend::advance_binding_index_epoch();\n";
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
        if (EnttSystemEmitter::requires_entt_hierarchy_helpers(program)) {
            out << "    (void)" << EnttCodegenUtils::hierarchy_index_expr(program) << ";\n";
        }
        // Every handle first, so initializers can name any entity of the module.
        for (const auto* entity : EnttCodegenUtils::declared_entities(program)) {
            out << "    " << EnttCodegenUtils::named_slot_name(entity_symbol(*entity, program))
                << " = cactus::runtime::entt_backend::create_entity(registry);\n";
        }
        // A parent is created before its children, so creation order stays a topological order.
        for (const auto* entity : parent_first_order(EnttCodegenUtils::declared_entities(program)).order) {
            const auto slot   = EnttCodegenUtils::named_slot_name(entity_symbol(*entity, program));
            const auto create = archetype_create_at_function_name(program.module_name, entity->name).ref() +
                                "(registry, " + slot + ");\n";
            if (!archetype_has_ranged_traits(entity->traits, entity->children, program)) {
                out << "    " << create;
                continue;
            }
            // A declared entity whose ranged value fails is not created, so its handle goes stale.
            out << "    if (" << archetype_range_precheck_function_name(program.module_name, entity->name).ref()
                << "(registry)) {\n";
            out << "        " << create;
            out << "    } else {\n";
            out << "        cactus::runtime::entt_backend::release_entity(registry, " << slot << ");\n";
            out << "    }\n";
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
        out << "            std::vector<entt::entity> hits;\n";
        out << "            for (auto entity : view) {\n";
        out << "                const auto& xform = reg.get<" << wt2d_cpp << ">(entity);\n";
        out << "                const auto& box   = reg.get<" << box_cpp << ">(entity);\n";
        out << "                if (world_pos.x < xform.position.x || world_pos.x > xform.position.x + box.size.x) { "
               "continue; }\n";
        out << "                if (world_pos.y < xform.position.y || world_pos.y > xform.position.y + box.size.y) { "
               "continue; }\n";
        out << "                hits.push_back(entity);\n";
        out << "            }\n";
        out << "            return cactus::runtime::entt_backend::oldest(reg, hits);\n";
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
        out << "            const int index = cactus::runtime::entt_backend::editor_template_index(name);\n";
        out << "            if (index < 0) { return entt::entity{entt::null}; }\n";
        out << "            auto entity = "
               "::cactus_gen::detail::cactus_template_registry[static_cast<std::size_t>(index)].create(reg);\n";
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
        out << "            cactus::runtime::entt_backend::Closest closest(reg);\n";
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
        out << "                if (hit.hit) { closest.offer(entity, hit.distance); }\n";
        out << "            }\n";
        out << "            return closest.entity();\n";
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
                out << "                auto cactus_gen_cam2d = cactus::runtime::entt_backend::get_active_camera_2d();\n";
                out << "                if (cactus_gen_cam2d.zoom == 0.0F) {\n";
                out << "                    for (const auto& [cactus_gen_e, cactus_gen_vp, cactus_gen_cam] : reg.view<" << vp_cpp_rig << ", "
                    << cam2d_cpp << ">().each()) {\n";
                out << "                        if (cactus_gen_vp.active) {\n";
                out << "                            cactus_gen_cam2d.target = cactus_gen_cam.offset;\n";
                out << "                            cactus_gen_cam2d.zoom = (cactus_gen_cam.zoom == 0.0F) ? 1.0F : cactus_gen_cam.zoom;\n";
                out << "                            break;\n";
                out << "                        }\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                if (cactus_gen_cam2d.zoom == 0.0F) { cactus_gen_cam2d.zoom = 1.0F; }\n";
                out << "                std::vector<entt::entity> cactus_gen_saved;\n";
                out << "                for (auto cactus_gen_ent : reg.view<" << vp_cpp_rig << ">()) {\n";
                out << "                    if (auto* cactus_gen_vp = reg.try_get<" << vp_cpp_rig
                    << ">(cactus_gen_ent); cactus_gen_vp != nullptr && cactus_gen_vp->active) {\n";
                out << "                        cactus_gen_saved.push_back(cactus_gen_ent); cactus_gen_vp->active = false;\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                "
                       "cactus::runtime::entt_backend::set_editor_saved_viewports(std::move(cactus_gen_saved));\n";
                out << "                auto cactus_gen_rig = cactus::runtime::entt_backend::create_entity(reg);\n";
                // The editor camera rig is created outside the archetype/spawn choke point in
                // emit_archetype_trait_initializers, so it must independently receive a creation
                // ordinal: any pairs: rule binding on a trait this rig also carries (e.g.
                // Viewport) would otherwise match it in emit_pair_binding_snapshot's view and
                // read a CreationOrdinal component that was never emplaced (UB).
                out << "                reg.emplace<cactus::runtime::entt_backend::CreationOrdinal>(cactus_gen_rig, "
                       "cactus::runtime::entt_backend::CreationOrdinal{.value = "
                       "cactus::runtime::entt_backend::generated_next_creation_ordinal()});\n";
                out << "                reg.emplace<" << ec2d_cpp_rig << ">(cactus_gen_rig, " << ec2d_cpp_rig
                    << "{.view_center = cactus_gen_cam2d.target, .zoom = cactus_gen_cam2d.zoom, .pan_speed = 1.0F, .zoom_speed = 0.1F, "
                       ".min_zoom = 0.05F, .max_zoom = 20.0F});\n";
                out << "                reg.emplace<" << cam2d_cpp << ">(cactus_gen_rig, " << cam2d_cpp
                    << "{.zoom = cactus_gen_cam2d.zoom, .offset = cactus_gen_cam2d.target});\n";
                out << "                reg.emplace<" << vp_cpp_rig << ">(cactus_gen_rig);\n";
                out << "                return cactus_gen_rig;\n";
                out << "            }\n";
            }
            if (rig_is_3d) {
                out << "            if (use_3d) {\n";
                out << "                auto cactus_gen_cam3d = cactus::runtime::entt_backend::get_active_camera_3d();\n";
                out << "                if (cactus_gen_cam3d.fovy == 0.0F) {\n";
                out << "                    for (const auto& [cactus_gen_e, cactus_gen_vp, cactus_gen_cam, cactus_gen_wt] : reg.view<" << vp_cpp_rig << ", "
                    << cam3d_cpp << ", " << wt3d_cpp << ">().each()) {\n";
                out << "                        if (cactus_gen_vp.active) {\n";
                out << "                            cactus_gen_cam3d.fovy = cactus_gen_cam.fov_y;\n";
                out << "                            cactus_gen_cam3d.position = cactus_gen_wt.position;\n";
                out << "                            const Vector3 cactus_gen_fwd = "
                       "cactus::runtime::stdlib::math::quat::forward(cactus_gen_wt.rotation);\n";
                out << "                            cactus_gen_cam3d.target = {.x = cactus_gen_wt.position.x + cactus_gen_fwd.x, .y = "
                       "cactus_gen_wt.position.y + cactus_gen_fwd.y, .z = cactus_gen_wt.position.z + cactus_gen_fwd.z};\n";
                out << "                            break;\n";
                out << "                        }\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                if (cactus_gen_cam3d.fovy == 0.0F) {\n";
                out << "                    cactus_gen_cam3d.fovy = 60.0F;\n";
                out << "                    cactus_gen_cam3d.position = Vector3{.x = 0.0F, .y = 5.0F, .z = 10.0F};\n";
                out << "                    cactus_gen_cam3d.target = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};\n";
                out << "                }\n";
                out << "                const Vector3 cactus_gen_pos = cactus_gen_cam3d.position;\n";
                out << "                const Vector3 cactus_gen_tgt = cactus_gen_cam3d.target;\n";
                out << "                const float cactus_gen_dx = cactus_gen_pos.x - cactus_gen_tgt.x;\n";
                out << "                const float cactus_gen_dy = cactus_gen_pos.y - cactus_gen_tgt.y;\n";
                out << "                const float cactus_gen_dz = cactus_gen_pos.z - cactus_gen_tgt.z;\n";
                out << "                const float cactus_gen_dist = cactus::runtime::stdlib::math::vec3::length(Vector3{.x = "
                       "cactus_gen_dx, .y = cactus_gen_dy, .z = cactus_gen_dz});\n";
                out << "                const float cactus_gen_distance = (cactus_gen_dist < 0.1F) ? 0.1F : cactus_gen_dist;\n";
                out << "                const float cactus_gen_sin_pitch = std::clamp(cactus_gen_dy / cactus_gen_distance, -1.0F, 1.0F);\n";
                out << "                const float cactus_gen_pitch = cactus::runtime::stdlib::math::atan2(cactus_gen_sin_pitch, "
                       "std::sqrt(1.0F - (cactus_gen_sin_pitch * cactus_gen_sin_pitch)));\n";
                out << "                const float cactus_gen_yaw = cactus::runtime::stdlib::math::atan2(-cactus_gen_dx, -cactus_gen_dz);\n";
                out << "                const Quat cactus_gen_rot = cactus::runtime::stdlib::math::quat::from_euler(cactus_gen_pitch, "
                       "cactus_gen_yaw, 0.0F);\n";
                out << "                std::vector<entt::entity> cactus_gen_saved;\n";
                out << "                for (auto cactus_gen_ent : reg.view<" << vp_cpp_rig << ">()) {\n";
                out << "                    if (auto* cactus_gen_vp = reg.try_get<" << vp_cpp_rig
                    << ">(cactus_gen_ent); cactus_gen_vp != nullptr && cactus_gen_vp->active) {\n";
                out << "                        cactus_gen_saved.push_back(cactus_gen_ent); cactus_gen_vp->active = false;\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                "
                       "cactus::runtime::entt_backend::set_editor_saved_viewports(std::move(cactus_gen_saved));\n";
                out << "                auto cactus_gen_rig = cactus::runtime::entt_backend::create_entity(reg);\n";
                // See the matching comment in the 2D branch above: this rig is created outside
                // the archetype/spawn choke point and must independently receive an ordinal.
                out << "                reg.emplace<cactus::runtime::entt_backend::CreationOrdinal>(cactus_gen_rig, "
                       "cactus::runtime::entt_backend::CreationOrdinal{.value = "
                       "cactus::runtime::entt_backend::generated_next_creation_ordinal()});\n";
                out << "                reg.emplace<" << ec3d_cpp_rig << ">(cactus_gen_rig, " << ec3d_cpp_rig
                    << "{.focus = cactus_gen_tgt, .yaw = cactus_gen_yaw, .pitch = cactus_gen_pitch, .distance = cactus_gen_distance, .orbit_speed = "
                       "0.005F, .pan_speed = 0.002F, .zoom_speed = 0.1F, .min_pitch = -1.5F, .max_pitch = 1.5F, "
                       ".min_distance = 0.1F, .max_distance = 1000.0F});\n";
                out << "                reg.emplace<" << cam3d_cpp << ">(cactus_gen_rig, " << cam3d_cpp
                    << "{.fov_y = cactus_gen_cam3d.fovy, .near = 0.1F, .far = 1000.0F});\n";
                out << "                reg.emplace<" << vp_cpp_rig << ">(cactus_gen_rig);\n";
                out << "                reg.emplace<" << wt3d_cpp << ">(cactus_gen_rig, " << wt3d_cpp
                    << "{.position = cactus_gen_pos, .rotation = cactus_gen_rot, .scale = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}});\n";
                out << "                return cactus_gen_rig;\n";
                out << "            }\n";
            }
            out << "            return entt::entity{entt::null};\n";
        }
        out << "        });\n";
        out << "    cactus::runtime::entt_backend::register_editor_camera_exit_impl(\n";
        out << "        [](entt::registry& reg, entt::entity rig) {\n";
        out << "            cactus::runtime::entt_backend::release_entity(reg, rig);\n";
        out << "            for (auto cactus_gen_saved_ent : cactus::runtime::entt_backend::editor_saved_viewports()) {\n";
        out << "                if (auto* cactus_gen_vp = reg.try_get<" << vp_cpp_rig
            << ">(cactus_gen_saved_ent)) { cactus_gen_vp->active = true; }\n";
        out << "            }\n";
        out << "            cactus::runtime::entt_backend::set_editor_saved_viewports({});\n";
        out << "        });\n";
    }
    if (uses_editor && rig_is_2d && uses_viewport) {
        out << "    cactus::runtime::entt_backend::register_editor_apply_camera_2d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity rig, Vector2 view_center, float zoom) {\n";
        out << "            if (auto* cactus_gen_cam = reg.try_get<" << cam2d_cpp << ">(rig)) {\n";
        out << "                cactus_gen_cam->zoom = zoom;\n";
        out << "                cactus_gen_cam->offset = view_center;\n";
        out << "            }\n";
        out << "        });\n";
    }
    if (wt_usage.flat) {
        out << "    cactus::runtime::entt_backend::register_editor_entity_position_2d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity cactus_gen_eid) -> Vector2 {\n";
        out << "            if (reg.valid(cactus_gen_eid)) {\n";
        out << "                if (const auto* cactus_gen_wt = reg.try_get<" << wt2d_cpp << ">(cactus_gen_eid)) {\n";
        out << "                    return Vector2{.x = cactus_gen_wt->position.x, .y = cactus_gen_wt->position.y};\n";
        out << "                }\n";
        out << "            }\n";
        out << "            return Vector2{.x = 0.0F, .y = 0.0F};\n";
        out << "        });\n";
    }
    if (uses_editor && rig_is_3d && uses_viewport) {
        out << "    cactus::runtime::entt_backend::register_editor_apply_camera_3d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity rig, Vector3 position, Quat rotation) {\n";
        out << "            if (auto* cactus_gen_wt = reg.try_get<" << wt3d_cpp << ">(rig)) {\n";
        out << "                cactus_gen_wt->position = position;\n";
        out << "                cactus_gen_wt->rotation = rotation;\n";
        out << "            }\n";
        out << "        });\n";
    }
    if (rig_is_3d) {
        out << "    cactus::runtime::entt_backend::register_editor_entity_position_3d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity cactus_gen_eid) -> Vector3 {\n";
        out << "            if (reg.valid(cactus_gen_eid)) {\n";
        out << "                if (const auto* cactus_gen_wt = reg.try_get<" << wt3d_cpp << ">(cactus_gen_eid)) {\n";
        out << "                    return cactus_gen_wt->position;\n";
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
            out << "            std::vector<cactus::runtime::entt_backend::PointerCandidate> cactus_gen_result;\n";
            out << "            auto view = reg.view<" << cl_cpp << ", " << ptarget_cpp << ">();\n";
            out << "            for (auto entity : view) {\n";
            out << "                const auto& layout = view.get<" << cl_cpp << ">(entity);\n";
            out << "                const auto& target = view.get<" << ptarget_cpp << ">(entity);\n";
            out << "                if (!layout.effective_visible) { continue; }\n";
            out << "                const Vector2 cactus_gen_max = Vector2{.x = layout.position.x + layout.size.x,\n";
            out << "                                              .y = layout.position.y + layout.size.y};\n";
            out << "                if (!cactus::runtime::entt_backend::point_in_rect(pointer_pos, layout.position, "
                   "cactus_gen_max)) { continue; }\n";
            out << "                if (!cactus::runtime::entt_backend::point_in_rect(\n";
            out << "                        pointer_pos, layout.clip_min, layout.clip_max)) { continue; }\n";
            out << "                cactus_gen_result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
            out << "                    .entity = entity, .enabled = target.enabled, .blocks_lower = "
                   "target.blocks_lower,\n";
            out << "                    .priority = target.priority, .draw_order = layout.draw_order,\n";
            out << "                    .creation_ordinal = "
                   "cactus::runtime::entt_backend::creation_ordinal(reg, entity)});\n";
            out << "            }\n";
            out << "            cactus::runtime::entt_backend::sort_window_pointer_candidates(cactus_gen_result);\n";
            out << "            return cactus_gen_result;\n";
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
            out << "            std::vector<cactus::runtime::entt_backend::PointerCandidate> cactus_gen_result;\n";
            out << "            const Vector2 cactus_gen_world_pos = "
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
                out << "                            cactus_gen_world_pos, xform.position, box.size)) { continue; }\n";
                out << "                    cactus_gen_result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .creation_ordinal = "
                       "cactus::runtime::entt_backend::creation_ordinal(reg, entity)});\n";
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
                out << "                            cactus_gen_world_pos, xform.position, circle.radius)) { continue; }\n";
                out << "                    cactus_gen_result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .creation_ordinal = "
                       "cactus::runtime::entt_backend::creation_ordinal(reg, entity)});\n";
                out << "                }\n";
                out << "            }\n";
            }
            out << "            cactus::runtime::entt_backend::sort_flat_world_pointer_candidates(cactus_gen_result);\n";
            out << "            return cactus_gen_result;\n";
            out << "        });\n";
        } else if (wt_usage.volume && (has_volume_box || has_volume_sphere)) {
            out << "    cactus::runtime::entt_backend::register_pointer_world_candidates_impl(\n";
            out << "        [](entt::registry& reg, Vector2 pointer_screen_pos) -> "
                   "std::vector<cactus::runtime::entt_backend::PointerCandidate> {\n";
            out << "            std::vector<cactus::runtime::entt_backend::PointerCandidate> cactus_gen_result;\n";
            out << "            const Ray cactus_gen_ray = cactus::runtime::entt_backend::screen_to_world_ray(\n";
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
                out << "                    const BoundingBox cactus_gen_bbox{\n";
                out << "                        .min = Vector3{.x = xform.position.x - (box.size.x * 0.5F),\n";
                out << "                                       .y = xform.position.y - (box.size.y * 0.5F),\n";
                out << "                                       .z = xform.position.z - (box.size.z * 0.5F)},\n";
                out << "                        .max = Vector3{.x = xform.position.x + (box.size.x * 0.5F),\n";
                out << "                                       .y = xform.position.y + (box.size.y * 0.5F),\n";
                out << "                                       .z = xform.position.z + (box.size.z * 0.5F)}};\n";
                out << "                    const RayCollision cactus_gen_hit = GetRayCollisionBox(cactus_gen_ray, cactus_gen_bbox);\n";
                out << "                    if (!cactus_gen_hit.hit || cactus_gen_hit.distance <= 0.0F) { continue; }\n";
                out << "                    cactus_gen_result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .distance = cactus_gen_hit.distance,\n";
                out << "                        .creation_ordinal = "
                       "cactus::runtime::entt_backend::creation_ordinal(reg, entity)});\n";
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
                out << "                    const RayCollision cactus_gen_hit =\n";
                out << "                        GetRayCollisionSphere(cactus_gen_ray, xform.position, sphere.radius);\n";
                out << "                    if (!cactus_gen_hit.hit || cactus_gen_hit.distance <= 0.0F) { continue; }\n";
                out << "                    cactus_gen_result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .distance = cactus_gen_hit.distance,\n";
                out << "                        .creation_ordinal = "
                       "cactus::runtime::entt_backend::creation_ordinal(reg, entity)});\n";
                out << "                }\n";
                out << "            }\n";
            }
            out << "            cactus::runtime::entt_backend::sort_volume_world_pointer_candidates(cactus_gen_result);\n";
            out << "            return cactus_gen_result;\n";
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
        if (delivery.cyclic()) {
            out << "    activation.current_delayed = &generated_scheduler_state().delayed_host;\n";
        }
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
    // the cactus_gen_translate_camera_2d/3d functions emitted early.
    const std::string vp_cpp = EnttCodegenUtils::trait_cpp_name("Viewport", program);

    out << "void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher) {\n";
    out << "    (void)dispatcher;\n";
    if (!uses_viewport) {
        out << "    (void)registry;\n";
    }
    out << "    cactus::runtime::entt_backend::begin_render_frame();\n";

    if (uses_viewport) {
        out << "    {\n";
        out << "        const int cactus_gen_sw = cactus::runtime::raylib::GetScreenWidth();\n";
        out << "        const int cactus_gen_sh = cactus::runtime::raylib::GetScreenHeight();\n";
        out << "        static std::vector<std::pair<int,entt::entity>> cactus_gen_vps;\n";
        out << "        cactus_gen_vps.clear();\n";
        out << "        for (const auto& [cactus_gen_vp_e, cactus_gen_vp] : registry.view<" << vp_cpp << ">().each()) {\n";
        out << "            if (cactus_gen_vp.active) { cactus_gen_vps.emplace_back(cactus_gen_vp.depth, cactus_gen_vp_e); }\n";
        out << "        }\n";
        out << "        std::ranges::sort(cactus_gen_vps);\n";
        out << "        for (auto& [cactus_gen_depth, cactus_gen_vp_ent] : cactus_gen_vps) {\n";
        out << "            (void)cactus_gen_depth;\n";
        out << "            const auto& cactus_gen_vp = registry.get<" << vp_cpp << ">(cactus_gen_vp_ent);\n";
        out << "            cactus::runtime::raylib::BeginScissorMode(\n";
        out << "                static_cast<int>(cactus_gen_vp.x * static_cast<float>(cactus_gen_sw)),\n";
        out << "                static_cast<int>(cactus_gen_vp.y * static_cast<float>(cactus_gen_sh)),\n";
        out << "                static_cast<int>(cactus_gen_vp.width * static_cast<float>(cactus_gen_sw)),\n";
        out << "                static_cast<int>(cactus_gen_vp.height * static_cast<float>(cactus_gen_sh)));\n";
        out << "            if (cactus_gen_vp.clear) { cactus::runtime::raylib::ClearBackground(cactus_gen_vp.clear_color); }\n";
        // Camera helper selection keys on the viewport entity's resolved camera
        // component type (flat vs volume Camera), not a shared bare Camera token.
        if (emit_2d_helper) {
            out << "            if (registry.all_of<" << cam2d_cpp << ">(cactus_gen_vp_ent)) {\n";
            out << "                const auto& cactus_gen_cam = registry.get<" << cam2d_cpp << ">(cactus_gen_vp_ent);\n";
            out << "                cactus::runtime::entt_backend::set_active_camera_2d(\n";
            out << "                    cactus_gen_translate_camera_2d(cactus_gen_cam, cactus_gen_sw, cactus_gen_sh));\n";
            out << "            }\n";
        }
        if (emit_3d_helper) {
            if (emit_2d_helper) {
                out << "            else if (registry.all_of<" << cam3d_cpp << ">(cactus_gen_vp_ent)) {\n";
            } else {
                out << "            if (registry.all_of<" << cam3d_cpp << ">(cactus_gen_vp_ent)) {\n";
            }
            out << "                const auto& cactus_gen_cam = registry.get<" << cam3d_cpp << ">(cactus_gen_vp_ent);\n";
            out << "                cactus::runtime::entt_backend::set_active_camera_3d(\n";
            out << "                    cactus_gen_translate_camera_3d(cactus_gen_vp_ent, cactus_gen_cam, registry));\n";
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

    out << "    ::cactus_gen::detail::clear_projected_traits(registry);\n";
    out << "}\n";
    out << "\n}  // namespace cactus::runtime::entt_backend\n";

    out << emit_backend_main(program);

    auto code = out.str();
    EnttSystemEmitter::assign_static_stores(code);
    return code;
}

}  // namespace cactus
