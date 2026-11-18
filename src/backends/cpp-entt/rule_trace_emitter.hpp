#pragma once

// Codegen for --trace-rules: calls of the run-time hooks in
// backends/cpp-entt/rule_trace.hpp. Nothing is added for a program compiled
// without the option.

#include "frontend/semantic_analyzer.hpp"

#include <string>
#include <string_view>

namespace cactus::rule_trace {

// One statement calling the hook `call` (e.g. `rule_trace_queue(kind)`).
[[nodiscard]] std::string hook(const DecoratedProgram& program, std::string_view call, std::string_view indent);

// The overload of `generated_trace_event_name` giving the occurrence type `type` the name `name`.
[[nodiscard]] std::string event_name(const DecoratedProgram& program, std::string_view type, std::string_view name);

// The drop callback that records a no-match for an occurrence whose recipient is gone.
[[nodiscard]] std::string drop_callback(const DecoratedProgram& program);

// `call`, the dispatch of one handler, inside a scope that records it.
[[nodiscard]] std::string wrap_handler_call(const DecoratedProgram& program,
                                            const HandlerIdentity& identity,
                                            std::string call);

}  // namespace cactus::rule_trace
