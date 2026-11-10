// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_PARALLEL_PAIRS
#error "CACTUS_PARALLEL_PAIRS must list name|sequential-exe|parallel-exe entries"
#endif

#include "subprocess_command.hpp"

#include <catch2/catch_test_macros.hpp>

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
    std::string sequential;
    std::string parallel;
};

std::vector<Pair> pairs() {
    std::vector<Pair> result;
    for (const auto entry : std::string_view{CACTUS_PARALLEL_PAIRS} | std::views::split(',')) {
        std::vector<std::string> parts;
        for (const auto part : entry | std::views::split('|')) {
            parts.emplace_back(part.begin(), part.end());
        }
        REQUIRE(parts.size() == 3);
        result.push_back(Pair{.name = parts[0], .sequential = parts[1], .parallel = parts[2]});
    }
    return result;
}

std::vector<std::string> digests(const std::string& executable, const fs::path& file, const std::string& workers) {
    REQUIRE(cactus_test::run_quoted(executable, {file.string(), workers}) == 0);
    std::ifstream in(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST_CASE("a parallel build gives the sequential build's world after every frame, on any worker count",
          "[parallel-handlers][subprocess]") {
    const auto root = fs::temp_directory_path() / "cactus_parallel_equivalence";
    fs::remove_all(root);
    fs::create_directories(root);
    for (const auto& pair : pairs()) {
        INFO(pair.name);
        const auto expected = digests(pair.sequential, root / (pair.name + ".sequential.txt"), "default");
        REQUIRE_FALSE(expected.empty());
        for (const std::string workers : {"0", "1", "2", "7", "default"}) {
            INFO("workers " << workers);
            CHECK(digests(pair.parallel, root / (pair.name + ".parallel." + workers + ".txt"), workers) == expected);
        }
    }
    fs::remove_all(root);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
