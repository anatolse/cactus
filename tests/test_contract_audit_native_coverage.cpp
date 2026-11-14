// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "frontend/native_field_reads.hpp"
#include "stdlib_modules.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

// Each audited headless test writes the natives it called to
// CACTUS_CONTRACT_AUDIT_DIR/<test>.natives when it exits.

namespace {

std::set<std::string> audited_natives(const std::filesystem::path& directory) {
    std::set<std::string> called;
    if (!std::filesystem::is_directory(directory)) {
        return called;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".natives") {
            continue;
        }
        std::ifstream file(entry.path());
        for (std::string line; std::getline(file, line);) {
            if (!line.empty()) {
                called.insert(line);
            }
        }
    }
    return called;
}

// The summarized natives no audited test called.
std::vector<std::string> unaudited_natives(const std::vector<std::string>& natives, const std::set<std::string>& called) {
    std::vector<std::string> missing;
    for (const auto& native : natives) {
        if (cactus::native_field_reads(native) != nullptr && !called.contains(native)) {
            missing.push_back(native);
        }
    }
    return missing;
}

}  // namespace

TEST_CASE("Contract audit: every summarized native runs under audit", "[contract-audit]") {
    const auto natives = cactus_test::world_natives(cactus_test::parse_stdlib());
    REQUIRE(natives.size() >= 10);
    CHECK(unaudited_natives(natives, audited_natives(CACTUS_CONTRACT_AUDIT_DIR)) == std::vector<std::string>{});
}

TEST_CASE("Contract audit: a summarized native no audited test called is named", "[contract-audit]") {
    const std::vector<std::string> natives{"std.query.count", "std.query.exists", "std.math.vec3.length"};
    CHECK(unaudited_natives(natives, {"std.query.exists"}) == std::vector<std::string>{"std.query.count"});
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
