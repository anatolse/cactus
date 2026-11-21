// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "linked_program.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace cactus_test;
namespace fs = std::filesystem;

namespace {

LinkedProgram link_game(const std::string& name, const std::string& body) {
    const auto dir = fs::path(CACTUS_TEST_BINARY_DIR) / "time_spans" / name;
    return link_sources(dir,
                        {{"game",
                          "module game\n"
                          "use std.core\n"
                          "use std.time as time\n" +
                              body}},
                        "game");
}

bool mentions_conflict(const LinkedProgram& linked) {
    return std::ranges::any_of(linked.diagnostics,
                               [](const auto& diagnostic) { return diagnostic.message.contains("conflict"); });
}

}  // namespace

TEST_CASE("std.time: a program with plain spans only compiles", "[semantic][stdlib-time-spans]") {
    const auto linked = link_game("plain_only",
                                  "trait Fuse\n"
                                  "entity Wick:\n"
                                  "    Fuse\n"
                                  "    time.Span:\n"
                                  "        length = 100ms\n"
                                  "rule Light:\n"
                                  "    each:\n"
                                  "        Fuse\n"
                                  "        time.Span as span\n"
                                  "    on time.SpanEnded:\n"
                                  "        emit time.RestartSpan to self\n");
    INFO(linked.describe());
    CHECK_FALSE(linked.has_errors());
    CHECK_FALSE(mentions_conflict(linked));
}

TEST_CASE("std.time: a program whose only span expires compiles", "[semantic][stdlib-time-spans]") {
    const auto linked = link_game("expires_only",
                                  "trait Spark\n"
                                  "template SparkTemplate:\n"
                                  "    Spark\n"
                                  "    time.Span:\n"
                                  "        length = 1s\n"
                                  "    time.Ending.Expires\n"
                                  "rule Emit:\n"
                                  "    on fixed_tick:\n"
                                  "        spawn SparkTemplate:\n"
                                  "            Spark\n");
    INFO(linked.describe());
    CHECK_FALSE(linked.has_errors());
    CHECK_FALSE(mentions_conflict(linked));
}

TEST_CASE("std.time: a program with every ending on both clocks compiles", "[semantic][stdlib-time-spans]") {
    const auto linked = link_game("all_endings",
                                  "template Fuse:\n"
                                  "    time.Span:\n"
                                  "        length = 100ms\n"
                                  "entity Plain:\n"
                                  "    use Fuse\n"
                                  "entity PlainReal:\n"
                                  "    use Fuse\n"
                                  "    time.RealTime\n"
                                  "entity Gone:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Expires\n"
                                  "entity GoneReal:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Expires\n"
                                  "    time.RealTime\n"
                                  "entity Loop:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Repeats\n"
                                  "entity LoopReal:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Repeats\n"
                                  "    time.RealTime\n"
                                  "entity Swing:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Bounces\n"
                                  "entity SwingReal:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Bounces\n"
                                  "    time.RealTime\n"
                                  "entity Held:\n"
                                  "    use Fuse\n"
                                  "    time.Paused\n"
                                  "rule Kick:\n"
                                  "    each:\n"
                                  "        time.Span as span\n"
                                  "    on fixed_tick:\n"
                                  "        if time.progress(span) > 0.5:\n"
                                  "            emit time.RestartSpan to self\n");
    INFO(linked.describe());
    CHECK_FALSE(linked.has_errors());
    CHECK_FALSE(mentions_conflict(linked));
}

TEST_CASE("std.time: SpanEnded handlers of plain and repeating spans need no order", "[semantic][stdlib-time-spans]") {
    const auto linked = link_game("ended_cascades",
                                  "trait Tally:\n"
                                  "    var last: int = 0\n"
                                  "entity Score:\n"
                                  "    Tally\n"
                                  "template Fuse:\n"
                                  "    time.Span:\n"
                                  "        length = 100ms\n"
                                  "entity Plain:\n"
                                  "    use Fuse\n"
                                  "entity PlainReal:\n"
                                  "    use Fuse\n"
                                  "    time.RealTime\n"
                                  "entity Loop:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Repeats\n"
                                  "entity LoopReal:\n"
                                  "    use Fuse\n"
                                  "    time.Ending.Repeats\n"
                                  "    time.RealTime\n"
                                  "rule Mark:\n"
                                  "    each:\n"
                                  "        time.Span\n"
                                  "    on time.SpanEnded:\n"
                                  "        Score[Tally].last = 1\n"
                                  "rule Reset:\n"
                                  "    after:\n"
                                  "        time.game_spans\n"
                                  "    on fixed_tick:\n"
                                  "        Score[Tally].last = 0\n");
    INFO(linked.describe());
    CHECK_FALSE(linked.has_errors());
    CHECK_FALSE(mentions_conflict(linked));
}

TEST_CASE("std.time: an entity with two endings is an error that names it", "[semantic][stdlib-time-spans]") {
    const auto linked = link_game("two_endings",
                                  "entity Both:\n"
                                  "    time.Span:\n"
                                  "        length = 1s\n"
                                  "    time.Ending.Expires\n"
                                  "    time.Ending.Repeats\n");
    INFO(linked.describe());
    REQUIRE(linked.has_errors());
    CHECK(std::ranges::any_of(linked.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.contains("'Both'") && diagnostic.message.contains("two variants of state 'Ending'");
    }));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
