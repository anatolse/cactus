#pragma once

// Run-time rule traces. Generated code calls these hooks only when compiled
// with --trace-rules; a build without it includes nothing from this header.

#include "backends/cpp-entt/runtime.hpp"

#include <entt/entity/entity.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cactus::runtime::entt_backend {

inline constexpr int kRuleTraceVersion                = 1;
inline constexpr std::size_t kDefaultRuleTraceCapacity = 4096;

// Gets one JSON line without its newline; false when the line could not be written.
using RuleTraceSink = std::function<bool(std::string_view line)>;

enum class RuleTraceRecordKind : std::uint8_t { Activation, Delivery, NoMatch, Invoke, Queue, Commit };

struct RuleTraceRecord {
    RuleTraceRecordKind kind{};
    std::uint64_t frame{};
    std::uint64_t activation{};
    std::string_view name;  // the root of an activation, an event or a handler
    std::string_view command;
    std::optional<entt::entity> recipient;
    std::uint64_t ordinal{};  // an activation's repetition, a commit's round
    std::uint64_t count{};    // a commit's commands
    std::optional<std::uint64_t> candidates;
    std::optional<std::uint64_t> runs;
};

struct RuleTraceOptions {
    std::optional<std::string> output;
    std::size_t capacity = kDefaultRuleTraceCapacity;
};

// Reads --trace-rules-output <path> and --trace-rules-capacity <positive-int>; any other argument is an error.
[[nodiscard]] std::expected<RuleTraceOptions, std::string> parse_rule_trace_options(
    std::span<const std::string_view> args);

// A sink that appends lines to `path`; it fails from the first line when the file can't be written.
[[nodiscard]] RuleTraceSink rule_trace_file_sink(const std::string& path);

// Starts a new trace: clears every record and counter and turns tracing on.
// With no sink, each frame's records are dropped at its end.
void configure_rule_trace(RuleTraceSink sink, std::size_t capacity = kDefaultRuleTraceCapacity);
void configure_rule_trace(const RuleTraceOptions& options);
// Configures tracing from a standalone runner's arguments; false, after printing why, when they are wrong.
[[nodiscard]] bool configure_rule_trace_from_args(int argc, char** argv);

// Writes the buffered records to the sink, then an overflow summary when records were dropped.
void flush_rule_trace();

[[nodiscard]] bool rule_trace_enabled();
// Empty unless a sink failure turned tracing off.
[[nodiscard]] const std::string& rule_trace_diagnostic();
[[nodiscard]] std::string format_rule_trace_record(const RuleTraceRecord& entry);

void rule_trace_frame_begin();
void rule_trace_frame_end();
void rule_trace_activation(std::string_view root, std::uint64_t repetition);
void rule_trace_delivery(std::string_view event, std::optional<entt::entity> recipient);
void rule_trace_no_match(std::string_view event, entt::entity recipient);
void rule_trace_queue(StructuralCommand::Kind kind);
void rule_trace_commit(std::size_t round, std::size_t commands);

// The rows the running handler's body counts; a handler that calls neither
// rule_trace_rows_counted nor rule_trace_single_row reports both counts as unavailable.
void rule_trace_rows_counted();
void rule_trace_candidate();
void rule_trace_run();
// One candidate that runs, for a handler with no selection.
void rule_trace_single_row();

struct RuleTraceCall {
    std::string_view handler;
    std::optional<std::size_t> slot;  // its invoke record, while that record is buffered
    RuleTraceCall* outer = nullptr;
};

// Records one handler call; commands it queues name it.
class RuleTraceHandlerScope {
public:
    RuleTraceHandlerScope(std::string_view handler, std::optional<entt::entity> recipient);
    RuleTraceHandlerScope(const RuleTraceHandlerScope&)            = delete;
    RuleTraceHandlerScope& operator=(const RuleTraceHandlerScope&) = delete;
    RuleTraceHandlerScope(RuleTraceHandlerScope&&)                 = delete;
    RuleTraceHandlerScope& operator=(RuleTraceHandlerScope&&)      = delete;
    ~RuleTraceHandlerScope();

private:
    RuleTraceCall call_;
};

}  // namespace cactus::runtime::entt_backend
