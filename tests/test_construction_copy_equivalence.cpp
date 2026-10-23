// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_EQUIVALENCE_PAIRS
#error "CACTUS_EQUIVALENCE_PAIRS must list name|chosen-exe|retained-exe entries"
#endif

#include "subprocess_command.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Pair {
    std::string name;
    std::string chosen;
    std::string retained;
};

std::vector<Pair> pairs() {
    std::vector<Pair> result;
    for (const auto entry : std::string_view{CACTUS_EQUIVALENCE_PAIRS} | std::views::split(',')) {
        std::vector<std::string> parts;
        for (const auto part : entry | std::views::split('|')) {
            parts.emplace_back(part.begin(), part.end());
        }
        REQUIRE(parts.size() == 3);
        result.push_back(Pair{.name = parts[0], .chosen = parts[1], .retained = parts[2]});
    }
    return result;
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<fs::path> files_in(const fs::path& directory) {
    std::vector<fs::path> names;
    for (const auto& entry : fs::directory_iterator(directory)) {
        names.push_back(entry.path().filename());
    }
    std::ranges::sort(names);
    return names;
}

}  // namespace

TEST_CASE("skipping unwritten construction copies leaves every save unchanged",
          "[persistence][provenance][subprocess]") {
    const auto root = fs::temp_directory_path() / "cactus_construction_equivalence";
    fs::remove_all(root);
    for (const auto& pair : pairs()) {
        INFO(pair.name);
        const auto chosen   = root / pair.name / "chosen";
        const auto retained = root / pair.name / "retained";
        fs::create_directories(chosen);
        fs::create_directories(retained);
        REQUIRE(cactus_test::run_quoted(pair.chosen, {chosen.string()}) == 0);
        REQUIRE(cactus_test::run_quoted(pair.retained, {retained.string()}) == 0);

        const auto names = files_in(chosen);
        REQUIRE(names == files_in(retained));
        CHECK(names.size() == 2);
        for (const auto& name : names) {
            INFO(name.string());
            CHECK(bytes_of(chosen / name) == bytes_of(retained / name));
        }
    }
    fs::remove_all(root);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
