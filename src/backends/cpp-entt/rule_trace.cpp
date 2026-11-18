#include "backends/cpp-entt/rule_trace.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <system_error>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

namespace {

struct RuleTraceState {
    RuleTraceSink sink;
    std::size_t capacity = kDefaultRuleTraceCapacity;
    std::vector<RuleTraceRecord> records;
    std::uint64_t dropped{};
    std::uint64_t frame{};
    std::uint64_t activation{};
    bool enabled        = true;
    bool header_written = false;
    std::string diagnostic;
    RuleTraceCall* call = nullptr;
};

RuleTraceState& state() {
    static RuleTraceState trace;
    return trace;
}

// The index of the buffered record, or nullopt when tracing is off or the buffer is full.
std::optional<std::size_t> record(RuleTraceRecord entry) {
    auto& trace = state();
    if (!trace.enabled) {
        return std::nullopt;
    }
    if (trace.records.size() >= trace.capacity) {
        ++trace.dropped;
        return std::nullopt;
    }
    entry.frame      = trace.frame;
    entry.activation = trace.activation;
    trace.records.push_back(entry);
    return trace.records.size() - 1;
}

// The invoke record of the running handler, or nullptr when it has none.
RuleTraceRecord* running_record() {
    auto& trace = state();
    if (trace.call == nullptr || !trace.call->slot.has_value()) {
        return nullptr;
    }
    return &trace.records[*trace.call->slot];
}

std::string_view command_name(StructuralCommand::Kind kind) {
    switch (kind) {
        case StructuralCommand::Kind::Spawn:
            return "spawn";
        case StructuralCommand::Kind::Destroy:
            return "destroy";
        case StructuralCommand::Kind::Add:
            return "add";
        case StructuralCommand::Kind::Remove:
            return "remove";
        case StructuralCommand::Kind::Set:
            return "set";
    }
    std::unreachable();
}

std::string_view record_name(RuleTraceRecordKind kind) {
    switch (kind) {
        case RuleTraceRecordKind::Activation:
            return "activation";
        case RuleTraceRecordKind::Delivery:
            return "deliver";
        case RuleTraceRecordKind::NoMatch:
            return "no-match";
        case RuleTraceRecordKind::Invoke:
            return "invoke";
        case RuleTraceRecordKind::Queue:
            return "queue";
        case RuleTraceRecordKind::Commit:
            return "commit";
    }
    std::unreachable();
}

void append_string(std::string& out, std::string_view text) {
    out += '"';
    for (const char character : text) {
        if (character == '"' || character == '\\') {
            out += '\\';
        }
        out += character;
    }
    out += '"';
}

void append_field(std::string& out, std::string_view key, std::string_view text) {
    out += ",\"";
    out += key;
    out += "\":";
    append_string(out, text);
}

void append_field(std::string& out, std::string_view key, std::uint64_t value) {
    std::format_to(std::back_inserter(out), ",\"{}\":{}", key, value);
}

void append_count(std::string& out, std::string_view key, const std::optional<std::uint64_t>& value) {
    if (value.has_value()) {
        append_field(out, key, *value);
    } else {
        append_field(out, key, std::string_view{"unavailable"});
    }
}

void append_recipient(std::string& out, const RuleTraceRecord& entry) {
    if (entry.recipient.has_value()) {
        append_field(out, "recipient", std::uint64_t{entity_bits(*entry.recipient)});
    }
}

void append_body(std::string& out, const RuleTraceRecord& entry) {
    switch (entry.kind) {
        case RuleTraceRecordKind::Activation:
            append_field(out, "root", entry.name);
            append_field(out, "repetition", entry.ordinal);
            return;
        case RuleTraceRecordKind::Delivery:
        case RuleTraceRecordKind::NoMatch:
            append_field(out, "event", entry.name);
            append_recipient(out, entry);
            return;
        case RuleTraceRecordKind::Invoke:
            append_field(out, "handler", entry.name);
            append_recipient(out, entry);
            append_count(out, "candidates", entry.candidates);
            append_count(out, "runs", entry.runs);
            return;
        case RuleTraceRecordKind::Queue:
            if (entry.name.empty()) {
                out += R"(,"handler":null)";
            } else {
                append_field(out, "handler", entry.name);
            }
            append_field(out, "command", entry.command);
            return;
        case RuleTraceRecordKind::Commit:
            append_field(out, "round", entry.ordinal);
            append_field(out, "commands", entry.count);
            return;
    }
    std::unreachable();
}

void append_record(std::string& out, const RuleTraceRecord& entry) {
    std::format_to(std::back_inserter(out), "{{\"frame\":{}", entry.frame);
    append_field(out, "activation", entry.activation);
    append_field(out, "record", record_name(entry.kind));
    append_body(out, entry);
    out += '}';
}

// Writes one line; a failed write turns tracing off.
bool write_line(RuleTraceState& trace, std::string_view line) {
    if (trace.sink(line)) {
        return true;
    }
    trace.enabled    = false;
    trace.diagnostic = "rule trace: cannot write the trace output; tracing is off";
    std::println(stderr, "{}", trace.diagnostic);
    return false;
}

bool write_records(RuleTraceState& trace) {
    if (!trace.header_written) {
        trace.header_written = true;
        if (!write_line(trace, std::format(R"({{"format":"cactus-rule-trace","version":{}}})", kRuleTraceVersion))) {
            return false;
        }
    }
    std::string line;
    for (const auto& entry : trace.records) {
        line.clear();
        append_record(line, entry);
        if (!write_line(trace, line)) {
            return false;
        }
    }
    if (trace.dropped == 0) {
        return true;
    }
    return write_line(trace,
                      std::format(R"({{"frame":{},"record":"overflow","dropped":{}}})", trace.frame, trace.dropped));
}

}  // namespace

std::expected<RuleTraceOptions, std::string> parse_rule_trace_options(std::span<const std::string_view> args) {
    RuleTraceOptions options;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const auto option = args[index];
        if (option != "--trace-rules-output" && option != "--trace-rules-capacity") {
            return std::unexpected("unknown option '" + std::string{option} + "'");
        }
        if (index + 1 >= args.size()) {
            return std::unexpected(std::string{option} + " requires an argument");
        }
        const auto value = args[++index];
        if (option == "--trace-rules-output") {
            options.output = std::string{value};
            continue;
        }
        std::size_t capacity{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), capacity);
        if (value.empty() || error != std::errc{} || end != value.data() + value.size() || capacity == 0) {
            return std::unexpected("--trace-rules-capacity needs a positive integer, not '" + std::string{value} + "'");
        }
        options.capacity = capacity;
    }
    return options;
}

RuleTraceSink rule_trace_file_sink(const std::string& path) {
    auto stream = std::make_shared<std::ofstream>(path, std::ios::trunc);
    return [stream](std::string_view line) {
        *stream << line << '\n';
        stream->flush();
        return stream->good();
    };
}

void configure_rule_trace(RuleTraceSink sink, std::size_t capacity) {
    auto& trace    = state();
    trace          = RuleTraceState{};
    trace.sink     = std::move(sink);
    trace.capacity = capacity;
    trace.records.reserve(std::min(capacity, kDefaultRuleTraceCapacity));
}

void configure_rule_trace(const RuleTraceOptions& options) {
    configure_rule_trace(options.output.has_value() ? rule_trace_file_sink(*options.output) : RuleTraceSink{},
                         options.capacity);
}

bool configure_rule_trace_from_args(int argc, char** argv) {
    const std::span<char*> given(argv, static_cast<std::size_t>(argc));
    std::vector<std::string_view> args;
    for (const char* arg : given.subspan(given.empty() ? 0 : 1)) {
        args.emplace_back(arg);
    }
    const auto options = parse_rule_trace_options(args);
    if (!options.has_value()) {
        std::println(stderr, "error: {}", options.error());
        return false;
    }
    configure_rule_trace(*options);
    return true;
}

void flush_rule_trace() {
    auto& trace = state();
    if (trace.enabled && trace.sink) {
        write_records(trace);
    }
    trace.records.clear();
    trace.dropped = 0;
    for (auto* call = trace.call; call != nullptr; call = call->outer) {
        call->slot.reset();
    }
}

bool rule_trace_enabled() {
    return state().enabled;
}

const std::string& rule_trace_diagnostic() {
    return state().diagnostic;
}

std::string format_rule_trace_record(const RuleTraceRecord& entry) {
    std::string out;
    append_record(out, entry);
    return out;
}

void rule_trace_frame_begin() {
    flush_rule_trace();
    ++state().frame;
}

void rule_trace_frame_end() {
    flush_rule_trace();
}

void rule_trace_activation(std::string_view root, std::uint64_t repetition) {
    auto& trace = state();
    if (!trace.enabled) {
        return;
    }
    ++trace.activation;
    record({.kind = RuleTraceRecordKind::Activation, .name = root, .ordinal = repetition});
}

void rule_trace_delivery(std::string_view event, std::optional<entt::entity> recipient) {
    record({.kind = RuleTraceRecordKind::Delivery, .name = event, .recipient = recipient});
}

void rule_trace_no_match(std::string_view event, entt::entity recipient) {
    record({.kind = RuleTraceRecordKind::NoMatch, .name = event, .recipient = recipient});
}

void rule_trace_queue(StructuralCommand::Kind kind) {
    const auto* call = state().call;
    record({.kind    = RuleTraceRecordKind::Queue,
            .name    = call != nullptr ? call->handler : std::string_view{},
            .command = command_name(kind)});
}

void rule_trace_commit(std::size_t round, std::size_t commands) {
    record({.kind = RuleTraceRecordKind::Commit, .ordinal = round, .count = commands});
}

void rule_trace_rows_counted() {
    if (auto* entry = running_record(); entry != nullptr) {
        entry->candidates = entry->candidates.value_or(0);
        entry->runs       = entry->runs.value_or(0);
    }
}

void rule_trace_candidate() {
    if (auto* entry = running_record(); entry != nullptr && entry->candidates.has_value()) {
        ++*entry->candidates;
    }
}

void rule_trace_run() {
    if (auto* entry = running_record(); entry != nullptr && entry->runs.has_value()) {
        ++*entry->runs;
    }
}

void rule_trace_single_row() {
    if (auto* entry = running_record(); entry != nullptr) {
        entry->candidates = entry->candidates.value_or(0) + 1;
        entry->runs       = entry->runs.value_or(0) + 1;
    }
}

RuleTraceHandlerScope::RuleTraceHandlerScope(std::string_view handler, std::optional<entt::entity> recipient)
    : call_{.handler = handler, .slot = std::nullopt, .outer = state().call} {
    state().call = &call_;
    call_.slot   = record({.kind = RuleTraceRecordKind::Invoke, .name = handler, .recipient = recipient});
}

RuleTraceHandlerScope::~RuleTraceHandlerScope() {
    state().call = call_.outer;
}

}  // namespace cactus::runtime::entt_backend
