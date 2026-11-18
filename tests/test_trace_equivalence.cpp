// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_TRACE_PAIRS
#error "CACTUS_TRACE_PAIRS must list name|untraced-exe|traced-exe entries"
#endif

#include "subprocess_command.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Pair {
    std::string name;
    std::string untraced;
    std::string traced;
};

std::vector<Pair> pairs() {
    std::vector<Pair> result;
    for (const auto entry : std::string_view{CACTUS_TRACE_PAIRS} | std::views::split(',')) {
        std::vector<std::string> parts;
        for (const auto part : entry | std::views::split('|')) {
            parts.emplace_back(part.begin(), part.end());
        }
        REQUIRE(parts.size() == 3);
        result.push_back(Pair{.name = parts[0], .untraced = parts[1], .traced = parts[2]});
    }
    return result;
}

std::vector<std::string> read_lines(const fs::path& file) {
    std::ifstream in(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> digests(const std::string& executable,
                                 const fs::path& file,
                                 const std::vector<std::string>& trace_args = {}) {
    std::vector<std::string> args{file.string()};
    args.insert(args.end(), trace_args.begin(), trace_args.end());
    REQUIRE(cactus_test::run_quoted(executable, args) == 0);
    return read_lines(file);
}

}  // namespace

TEST_CASE("a traced build gives the untraced build's world after every frame, and the same trace twice",
          "[rule-trace][subprocess]") {
    const auto root = fs::temp_directory_path() / "cactus_trace_equivalence";
    fs::remove_all(root);
    fs::create_directories(root);
    for (const auto& pair : pairs()) {
        INFO(pair.name);
        const auto expected = digests(pair.untraced, root / (pair.name + ".untraced.txt"));
        REQUIRE_FALSE(expected.empty());

        const auto traced = [&](const std::string& run, const char* capacity) {
            const auto trace = root / (pair.name + "." + run + ".jsonl");
            CHECK(digests(pair.traced,
                          root / (pair.name + "." + run + ".txt"),
                          {"--trace-rules-output", trace.string(), "--trace-rules-capacity", capacity}) == expected);
            return read_lines(trace);
        };
        const auto first = traced("trace", "4096");
        REQUIRE(first.size() > 2);
        CHECK(first.front() == R"({"format":"cactus-rule-trace","version":1})");
        CHECK(traced("again", "4096") == first);

        const auto small = traced("small", "3");
        CHECK(std::ranges::any_of(small,
                                  [](const std::string& line) { return line.contains(R"("record":"overflow")"); }));
    }
    fs::remove_all(root);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
