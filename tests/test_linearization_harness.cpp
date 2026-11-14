// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/linearization_harness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace backend = cactus::runtime::entt_backend;

// A -> C -> D; B and E have no edges.
constexpr std::array<std::string_view, 5> kHandlers{"m.A/on tick", "m.B/on tick", "m.C/on tick", "m.D/on tick",
                                                    "m.E/on tick"};
constexpr std::array<backend::LinearizationEdge, 2> kEdges{{{.before = 0, .after = 2}, {.before = 2, .after = 3}}};

std::vector<std::size_t> run_order() {
    std::vector<std::size_t> order;
    backend::dispatch_linear_extension(kHandlers, kEdges, [&](std::size_t index) { order.push_back(index); });
    return order;
}

struct HarnessReset {
    HarnessReset() {
        reset();
    }
    ~HarnessReset() {
        reset();
    }
    HarnessReset(const HarnessReset&)            = delete;
    HarnessReset& operator=(const HarnessReset&) = delete;
    HarnessReset(HarnessReset&&)                 = delete;
    HarnessReset& operator=(HarnessReset&&)      = delete;

    static void reset() {
        backend::set_linearization(std::nullopt);
        backend::set_linearization_swap(std::nullopt);
        backend::clear_linearization_pairs();
    }
};

bool listed(std::string_view first, std::string_view second) {
    return std::ranges::contains(backend::linearization_pairs(),
                                 backend::LinearizationPair{.first = std::string(first), .second = std::string(second)});
}

}  // namespace

TEST_CASE("a dispatch set lists exactly the pairs its edges leave unordered", "[linearization-harness]") {
    const HarnessReset reset;
    CHECK(run_order() == std::vector<std::size_t>{0, 1, 2, 3, 4});
    CHECK(backend::linearization_pairs().size() == 7);
    CHECK(listed("m.A/on tick", "m.B/on tick"));
    CHECK(listed("m.B/on tick", "m.C/on tick"));
    CHECK(listed("m.C/on tick", "m.E/on tick"));
}

TEST_CASE("a dispatch set lists no pair its edges order, directly or through another handler",
          "[linearization-harness]") {
    const HarnessReset reset;
    run_order();
    CHECK_FALSE(listed("m.A/on tick", "m.C/on tick"));
    CHECK_FALSE(listed("m.A/on tick", "m.D/on tick"));
    run_order();
    CHECK(backend::linearization_pairs().size() == 7);
}

TEST_CASE("a pair swap runs the pair adjacent after its predecessors, in either order", "[linearization-harness]") {
    const HarnessReset reset;
    backend::set_linearization_swap(backend::LinearizationPair{.first = "m.B/on tick", .second = "m.D/on tick"});
    CHECK(run_order() == std::vector<std::size_t>{0, 2, 1, 3, 4});
    backend::set_linearization_swap(backend::LinearizationPair{.first = "m.B/on tick", .second = "m.D/on tick"}, true);
    CHECK(run_order() == std::vector<std::size_t>{0, 2, 3, 1, 4});

    backend::set_linearization_swap(backend::LinearizationPair{.first = "m.D/on tick", .second = "m.E/on tick"}, true);
    CHECK(run_order() == std::vector<std::size_t>{0, 2, 4, 3, 1});
}

TEST_CASE("a pair swap leaves sets without both handlers in stable order", "[linearization-harness]") {
    const HarnessReset reset;
    backend::set_linearization_swap(backend::LinearizationPair{.first = "m.B/on tick", .second = "x.Y/on tick"}, true);
    CHECK(run_order() == std::vector<std::size_t>{0, 1, 2, 3, 4});
    backend::set_linearization_swap(backend::LinearizationPair{.first = "m.A/on tick", .second = "m.D/on tick"}, true);
    CHECK(run_order() == std::vector<std::size_t>{0, 1, 2, 3, 4});
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
