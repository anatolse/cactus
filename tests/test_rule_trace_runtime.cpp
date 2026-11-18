// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/rule_trace.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace cactus::runtime::entt_backend;

namespace {

using Lines = std::vector<std::string>;

RuleTraceSink collect_into(const std::shared_ptr<Lines>& collected) {
    return [collected](std::string_view line) {
        collected->emplace_back(line);
        return true;
    };
}

void run_one_handler() {
    rule_trace_activation("std.core.tick", 0);
    const RuleTraceHandlerScope scope("demo.Move/on std.core.tick", std::nullopt);
    rule_trace_rows_counted();
    rule_trace_candidate();
    rule_trace_run();
}

}  // namespace

TEST_CASE("a trace starts with its version and writes one JSON line per record", "[rule-trace]") {
    const auto collected = std::make_shared<Lines>();
    configure_rule_trace(collect_into(collected));
    rule_trace_frame_begin();
    run_one_handler();
    rule_trace_queue(StructuralCommand::Kind::Spawn);
    rule_trace_commit(1, 1);
    rule_trace_frame_end();

    REQUIRE(collected->size() == 5);
    CHECK(collected->at(0) == R"({"format":"cactus-rule-trace","version":1})");
    CHECK(collected->at(1) ==
          R"({"frame":1,"activation":1,"record":"activation","root":"std.core.tick","repetition":0})");
    CHECK(collected->at(2) ==
          R"({"frame":1,"activation":1,"record":"invoke","handler":"demo.Move/on std.core.tick","candidates":1,"runs":1})");
    CHECK(collected->at(3) == R"({"frame":1,"activation":1,"record":"queue","handler":null,"command":"spawn"})");
    CHECK(collected->at(4) == R"({"frame":1,"activation":1,"record":"commit","round":1,"commands":1})");
}

TEST_CASE("a handler that counts no rows reports its counts as unavailable", "[rule-trace]") {
    const auto collected = std::make_shared<Lines>();
    configure_rule_trace(collect_into(collected));
    rule_trace_frame_begin();
    rule_trace_activation("std.core.tick", 0);
    {
        const RuleTraceHandlerScope scope("demo.Pair/on std.core.tick", entt::entity{7});
        rule_trace_queue(StructuralCommand::Kind::Destroy);
    }
    rule_trace_frame_end();

    REQUIRE(collected->size() == 4);
    CHECK(collected->at(2) ==
          R"({"frame":1,"activation":1,"record":"invoke","handler":"demo.Pair/on std.core.tick","recipient":7,)"
          R"("candidates":"unavailable","runs":"unavailable"})");
    CHECK(collected->at(3) ==
          R"({"frame":1,"activation":1,"record":"queue","handler":"demo.Pair/on std.core.tick","command":"destroy"})");
}

TEST_CASE("records past the capacity are dropped and summed up at the end of the frame", "[rule-trace]") {
    const auto collected = std::make_shared<Lines>();
    configure_rule_trace(collect_into(collected), 3);
    rule_trace_frame_begin();
    for (int index = 0; index < 4; ++index) {
        run_one_handler();
    }
    rule_trace_frame_end();

    REQUIRE(collected->size() == 5);
    CHECK(collected->at(4) == R"({"frame":1,"record":"overflow","dropped":5})");

    collected->clear();
    rule_trace_frame_begin();
    run_one_handler();
    rule_trace_frame_end();
    REQUIRE(collected->size() == 2);
    CHECK(collected->at(0).starts_with(R"({"frame":2,"activation":5,"record":"activation")"));
}

TEST_CASE("a sink failure turns tracing off with a diagnostic", "[rule-trace]") {
    int calls = 0;
    configure_rule_trace([&calls](std::string_view) {
        ++calls;
        return false;
    });
    CHECK(rule_trace_enabled());
    CHECK(rule_trace_diagnostic().empty());
    rule_trace_frame_begin();
    run_one_handler();
    rule_trace_frame_end();
    CHECK_FALSE(rule_trace_enabled());
    CHECK(rule_trace_diagnostic().contains("tracing is off"));
    CHECK(calls == 1);

    rule_trace_frame_begin();
    run_one_handler();
    rule_trace_frame_end();
    CHECK(calls == 1);
}

TEST_CASE("an unwritable trace file turns tracing off", "[rule-trace]") {
    const auto directory = std::filesystem::temp_directory_path() / "cactus_rule_trace_dir";
    std::filesystem::create_directories(directory);
    configure_rule_trace(rule_trace_file_sink(directory.string()));
    rule_trace_frame_begin();
    run_one_handler();
    rule_trace_frame_end();
    CHECK_FALSE(rule_trace_enabled());
    CHECK_FALSE(rule_trace_diagnostic().empty());
    std::filesystem::remove_all(directory);
}

TEST_CASE("a trace file holds the lines a sink got", "[rule-trace]") {
    const auto path = std::filesystem::temp_directory_path() / "cactus_rule_trace_file.jsonl";
    std::filesystem::remove(path);
    configure_rule_trace(RuleTraceOptions{.output = path.string(), .capacity = 8});
    rule_trace_frame_begin();
    run_one_handler();
    rule_trace_frame_end();
    flush_rule_trace();
    configure_rule_trace(RuleTraceSink{});
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == R"({"format":"cactus-rule-trace","version":1})");
    in.close();
    std::filesystem::remove(path);
}

TEST_CASE("tracing with no sink keeps no history past a frame", "[rule-trace]") {
    configure_rule_trace(RuleTraceSink{}, 2);
    rule_trace_frame_begin();
    run_one_handler();
    run_one_handler();
    rule_trace_frame_end();
    CHECK(rule_trace_enabled());

    const auto collected = std::make_shared<Lines>();
    configure_rule_trace(collect_into(collected));
    rule_trace_frame_end();
    CHECK(collected->size() == 1);
}

TEST_CASE("the standalone runner's trace options", "[rule-trace]") {
    const auto parse = [](std::initializer_list<std::string_view> args) {
        const std::vector<std::string_view> list(args);
        return parse_rule_trace_options(list);
    };
    const auto defaults = parse({});
    REQUIRE(defaults.has_value());
    CHECK_FALSE(defaults->output.has_value());
    CHECK(defaults->capacity == kDefaultRuleTraceCapacity);

    const auto given = parse({"--trace-rules-output", "out.jsonl", "--trace-rules-capacity", "16"});
    REQUIRE(given.has_value());
    CHECK(given->output == "out.jsonl");
    CHECK(given->capacity == 16);

    for (const auto* bad : {"0", "-3", "ten", "", "12x", "99999999999999999999999"}) {
        INFO(bad);
        const auto rejected = parse({"--trace-rules-capacity", bad});
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().contains("--trace-rules-capacity needs a positive integer"));
    }
    CHECK_FALSE(parse({"--trace-rules-capacity"}).has_value());
    CHECK_FALSE(parse({"--trace-rules-output"}).has_value());
    CHECK(parse({"--unknown"}).error().contains("unknown option '--unknown'"));
}

TEST_CASE("the standalone runner configures tracing from its command line", "[rule-trace]") {
    std::string program = "game";
    std::string option  = "--trace-rules-capacity";
    std::string bad     = "0";
    std::array<char*, 3> rejected{program.data(), option.data(), bad.data()};
    CHECK_FALSE(configure_rule_trace_from_args(static_cast<int>(rejected.size()), rejected.data()));

    std::string good = "2";
    std::array<char*, 3> accepted{program.data(), option.data(), good.data()};
    REQUIRE(configure_rule_trace_from_args(static_cast<int>(accepted.size()), accepted.data()));
    CHECK(rule_trace_enabled());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
