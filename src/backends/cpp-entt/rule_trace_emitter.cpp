#include "backends/cpp-entt/rule_trace_emitter.hpp"

#include "backends/cpp-entt/type_utils.hpp"

namespace cactus::rule_trace {

namespace {

constexpr std::string_view kRuntime = "cactus::runtime::entt_backend::";

}  // namespace

std::string hook(const DecoratedProgram& program, std::string_view call, std::string_view indent) {
    if (!program.trace_rules) {
        return {};
    }
    return std::string{indent} + std::string{kRuntime} + std::string{call} + ";\n";
}

std::string event_name(const DecoratedProgram& program, std::string_view type, std::string_view name) {
    if (!program.trace_rules) {
        return {};
    }
    return "[[maybe_unused]] constexpr std::string_view generated_trace_event_name(const " + std::string{type} +
           "& /*occurrence*/) {\n    return " + EnttCodegenUtils::string_literal_cpp(name) + ";\n}\n\n";
}

std::string drop_callback(const DecoratedProgram& program) {
    if (!program.trace_rules) {
        return {};
    }
    return "[](const auto& occurrence, entt::entity target) { " + std::string{kRuntime} +
           "rule_trace_no_match(generated_trace_event_name(occurrence), target); }";
}

std::string wrap_handler_call(const DecoratedProgram& program, const HandlerIdentity& identity, std::string call) {
    if (!program.trace_rules) {
        return call;
    }
    const auto* recipient = identity.trigger.kind == HandlerTriggerKind::Phase ? "std::nullopt" : "target";
    return "    {\n    const " + std::string{kRuntime} + "RuleTraceHandlerScope cactus_trace_scope(" +
           EnttCodegenUtils::string_literal_cpp(identity.canonical_id()) + ", " + recipient + ");\n" + call + "    }\n";
}

}  // namespace cactus::rule_trace
