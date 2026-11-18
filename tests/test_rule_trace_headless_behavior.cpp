// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace backend = cactus::runtime::entt_backend;

using Lines = std::vector<std::string>;

// Also resets the scheduler, so a fixed phase's leftover time never carries over from another test.
std::shared_ptr<Lines> start_trace(std::size_t capacity = backend::kDefaultRuleTraceCapacity) {
    backend::generated_reset_scheduler_state();
    auto lines = std::make_shared<Lines>();
    backend::configure_rule_trace(
        [lines](std::string_view line) {
            lines->emplace_back(line);
            return true;
        },
        capacity);
    return lines;
}

bool has_all(std::string_view line, std::initializer_list<std::string_view> parts) {
    return std::ranges::all_of(parts, [line](std::string_view part) { return line.contains(part); });
}

Lines frame_lines(const Lines& lines, int frame) {
    const auto prefix = "{\"frame\":" + std::to_string(frame) + ",";
    Lines result;
    for (const auto& line : lines) {
        if (line.starts_with(prefix)) {
            result.push_back(line);
        }
    }
    return result;
}

// The lines of the activation with this root, from its activation record to the next one.
Lines activation_lines(const Lines& lines, std::string_view root, std::size_t which = 0) {
    Lines result;
    std::size_t seen = 0;
    bool inside      = false;
    for (const auto& line : lines) {
        if (line.contains(R"("record":"activation")")) {
            inside = line.contains(R"("root":")" + std::string{root} + '"') && seen++ == which;
        }
        if (inside) {
            result.push_back(line);
        }
    }
    return result;
}

// The index of the first line from `from` with every part, or nullopt.
std::optional<std::size_t> find_line(const Lines& lines,
                                     std::initializer_list<std::string_view> parts,
                                     std::size_t from = 0) {
    for (std::size_t index = from; index < lines.size(); ++index) {
        if (has_all(lines[index], parts)) {
            return index;
        }
    }
    return std::nullopt;
}

// The start of a line, up to its record kind: its frame and activation.
std::string activation_prefix(const std::string& line) {
    return line.substr(0, line.find(",\"record\""));
}

std::string entity_text(entt::entity entity) {
    return std::to_string(entt::to_integral(entity));
}

entt::entity player() {
    return cactus_gen::detail::generated_named_slots().gunfire.Player;
}

}  // namespace

TEST_CASE("a queued spawn becomes visible only after its activation commits", "[rule-trace][deferred-spawn]") {
    entt::registry registry;
    const auto trace = start_trace();
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);

    const auto tick = activation_lines(frame_lines(*trace, 1), "std.core.tick");
    const auto fire = find_line(tick, {R"("record":"invoke","handler":"gunfire.Fire/on std.core.tick")",
                                       R"("candidates":1,"runs":1)"});
    REQUIRE(fire.has_value());
    const auto queued = find_line(
        tick, {R"("record":"queue","handler":"gunfire.Fire/on std.core.tick","command":"spawn")"}, *fire);
    REQUIRE(queued.has_value());
    const auto move = find_line(
        tick,
        {R"("record":"invoke","handler":"gunfire.MoveBullet/on std.core.tick","candidates":0,"runs":0)"},
        *queued);
    REQUIRE(move.has_value());
    CHECK(find_line(tick, {R"("record":"commit","round":1,"commands":1)"}, *move).has_value());
    CHECK(registry.view<cactus_gen::gunfire::Bullet>().size() == 1);

    cactus_headless_test::drive_frame(registry);
    const auto next = activation_lines(frame_lines(*trace, 2), "std.core.tick");
    CHECK(find_line(next, {R"("handler":"gunfire.MoveBullet/on std.core.tick","candidates":1,"runs":1)"}).has_value());
    CHECK(find_line(next, {R"("handler":"gunfire.Fire/on std.core.tick","candidates":1,"runs":0)"}).has_value());
    CHECK_FALSE(find_line(next, {R"("record":"queue")"}).has_value());
}

TEST_CASE("two fixed repetitions of one frame are two activations with their own commits",
          "[rule-trace][repetitions]") {
    entt::registry registry;
    const auto trace = start_trace();
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry, 2.5F / 60.0F);

    const auto frame  = frame_lines(*trace, 1);
    const auto first  = activation_lines(frame, "std.core.fixed_tick", 0);
    const auto second = activation_lines(frame, "std.core.fixed_tick", 1);
    CHECK(activation_lines(frame, "std.core.fixed_tick", 2).empty());
    REQUIRE(first.size() == 3);
    REQUIRE(second.size() == 3);
    CHECK(first[0].contains(R"("repetition":0)"));
    CHECK(second[0].contains(R"("repetition":1)"));
    CHECK(activation_prefix(first[0]) != activation_prefix(second[0]));
    for (const auto* lines : {&first, &second}) {
        CHECK((*lines)[1].contains(R"("handler":"gunfire.Stepper/on std.core.fixed_tick","candidates":1,"runs":1)"));
        CHECK((*lines)[2].contains(R"("record":"commit","round":1,"commands":0)"));
        CHECK(activation_prefix((*lines)[1]) == activation_prefix((*lines)[0]));
        CHECK(activation_prefix((*lines)[2]) == activation_prefix((*lines)[0]));
    }
}

TEST_CASE("an event sent to a stale handle records a no-match with that handle", "[rule-trace][no-match]") {
    entt::registry registry;
    const auto trace = start_trace();
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);

    const auto tick = activation_lines(frame_lines(*trace, 1), "std.core.tick");
    const auto poke = find_line(tick, {R"("record":"invoke","handler":"gunfire.Poke/on std.core.tick")"});
    REQUIRE(poke.has_value());
    CHECK(find_line(tick,
                    {R"("record":"no-match","event":"gunfire.Poked","recipient":)" + entity_text(entt::null) + "}"},
                    *poke)
              .has_value());
    CHECK(registry.get<cactus_gen::gunfire::Aim>(player()).target == entt::entity{entt::null});
}

TEST_CASE("a cascade delivery names its activation, event and recipient", "[rule-trace][cascade]") {
    entt::registry registry;
    const auto trace = start_trace();
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);
    cactus_headless_test::dispatch_unload_event(registry);
    backend::flush_rule_trace();

    const auto load = frame_lines(*trace, 0);
    REQUIRE(load.size() >= 2);
    CHECK(load[0] == R"({"frame":0,"activation":1,"record":"activation","root":"std.core.load","repetition":0})");
    CHECK(load[1] == R"({"frame":0,"activation":1,"record":"deliver","event":"std.core.load"})");

    const auto tick      = activation_lines(frame_lines(*trace, 1), "std.core.tick");
    const auto recipient = entity_text(player());
    const auto deliver =
        find_line(tick, {R"("record":"deliver","event":"gunfire.Poked","recipient":)" + recipient + "}"});
    REQUIRE(deliver.has_value());
    CHECK(find_line(tick,
                    {R"("record":"invoke","handler":"gunfire.TakePoke/on gunfire.Poked","recipient":)" + recipient +
                         R"(,"candidates":1,"runs":1)"},
                    *deliver)
              .has_value());
    const auto first_id = activation_prefix(tick[0]);
    for (const auto& line : tick) {
        CHECK(line.starts_with(first_id));
    }

    std::uint64_t last = 0;
    for (const auto& line : *trace) {
        if (!line.contains(R"("record":"activation")")) {
            continue;
        }
        constexpr std::string_view key = "\"activation\":";
        const auto start               = line.find(key) + key.size();
        const auto id    = std::stoull(line.substr(start, line.find(',', start) - start));
        CHECK(id == last + 1);
        last = id;
    }
    const auto unload = activation_lines(*trace, "std.core.unload");
    CHECK(find_line(unload, {R"("handler":"std.core.SceneCleanup/on std.core.unload")"}).has_value());
    CHECK(find_line(unload, {R"("handler":"std.core.SceneCleanup/on std.core.unload","command":"destroy")"})
              .has_value());
}

TEST_CASE("a full trace buffer ends its frame with an overflow summary and the game goes on",
          "[rule-trace][overflow]") {
    entt::registry registry;
    const auto trace = start_trace(4);
    cactus_headless_test::init_and_load(registry);
    for (int frame = 0; frame < 5; ++frame) {
        cactus_headless_test::drive_frame(registry);
    }
    for (int frame = 1; frame <= 5; ++frame) {
        const auto lines = frame_lines(*trace, frame);
        REQUIRE(lines.size() == 5);
        CHECK(lines.back().starts_with("{\"frame\":" + std::to_string(frame) + ",\"record\":\"overflow\",\"dropped\":"));
    }
    const auto bullets = registry.view<cactus_gen::gunfire::Bullet>();
    REQUIRE(bullets.size() == 1);
    CHECK(registry.get<cactus_gen::gunfire::Bullet>(bullets.front()).x == 4.0F);
    CHECK(registry.get<cactus_gen::gunfire::Steps>(player()).count == 5);
}

TEST_CASE("two runs with the same inputs give the same trace", "[rule-trace][deterministic]") {
    const auto run = [] {
        entt::registry registry;
        const auto trace = start_trace();
        cactus_headless_test::init_and_load(registry);
        for (const float dt : {1.0F / 60.0F, 2.5F / 60.0F, 0.5F / 60.0F, 1.0F / 60.0F}) {
            cactus_headless_test::drive_frame(registry, dt);
        }
        cactus_headless_test::dispatch_unload_event(registry);
        backend::flush_rule_trace();
        return *trace;
    };
    const auto first = run();
    CHECK(first.size() > 20);
    CHECK(run() == first);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
