// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

constexpr int kSeed = 1337;
constexpr double kTolerance = 1e-5;

// An independent C++ transcription of the hash and Perlin recipe documented in
// stdlib/std/noise.cactus, so the test checks the documented algorithm and not
// only that the compiled Cactus agrees with itself.
namespace reference {

constexpr std::uint32_t kMultiplier = 73244475U;

std::int32_t wrap(std::int64_t value) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value & 0xFFFFFFFF));
}

std::int32_t floor_div(std::int32_t a, std::int32_t b) {
    std::int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

std::int32_t floor_mod(std::int32_t a, std::int32_t b) {
    return a - (floor_div(a, b) * b);
}

std::int32_t mix(std::int32_t h) {
    std::int32_t x = wrap(static_cast<std::int64_t>(h) * kMultiplier);
    x = wrap(static_cast<std::int64_t>(x) + floor_div(x, 65536));
    x = wrap(static_cast<std::int64_t>(x) * kMultiplier);
    x = wrap(static_cast<std::int64_t>(x) + floor_div(x, 65536));
    return x;
}

std::int32_t hash2(std::int32_t x, std::int32_t y, std::int32_t seed) {
    return mix(wrap(static_cast<std::int64_t>(mix(wrap(static_cast<std::int64_t>(mix(seed)) + x))) + y));
}

std::int32_t hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t seed) {
    return mix(wrap(static_cast<std::int64_t>(hash2(x, y, seed)) + z));
}

double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double lerp(double a, double b, double t) {
    return a + ((b - a) * t);
}

double grad2(std::int32_t h, double x, double y) {
    constexpr double kDiag = 0.70710678;
    switch (floor_mod(h, 8)) {
    case 0: return x;
    case 1: return -x;
    case 2: return y;
    case 3: return -y;
    case 4: return (x + y) * kDiag;
    case 5: return (y - x) * kDiag;
    case 6: return (x - y) * kDiag;
    default: return (-x - y) * kDiag;
    }
}

double grad3(std::int32_t h, double x, double y, double z) {
    switch (floor_mod(h, 12)) {
    case 0: return x + y;
    case 1: return y - x;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x + z;
    case 5: return z - x;
    case 6: return x - z;
    case 7: return -x - z;
    case 8: return y + z;
    case 9: return z - y;
    case 10: return y - z;
    default: return -y - z;
    }
}

double perlin2(double px, double py, std::int32_t seed) {
    const auto ix = static_cast<std::int32_t>(std::floor(px));
    const auto iy = static_cast<std::int32_t>(std::floor(py));
    const double fx = px - ix;
    const double fy = py - iy;
    const double n00 = grad2(hash2(ix, iy, seed), fx, fy);
    const double n10 = grad2(hash2(ix + 1, iy, seed), fx - 1.0, fy);
    const double n01 = grad2(hash2(ix, iy + 1, seed), fx, fy - 1.0);
    const double n11 = grad2(hash2(ix + 1, iy + 1, seed), fx - 1.0, fy - 1.0);
    const double value = lerp(lerp(n00, n10, fade(fx)), lerp(n01, n11, fade(fx)), fade(fy));
    return std::clamp(value * 1.41421356, -1.0, 1.0);
}

double perlin3(double px, double py, double pz, std::int32_t seed) {
    const auto ix = static_cast<std::int32_t>(std::floor(px));
    const auto iy = static_cast<std::int32_t>(std::floor(py));
    const auto iz = static_cast<std::int32_t>(std::floor(pz));
    const double fx = px - ix;
    const double fy = py - iy;
    const double fz = pz - iz;
    std::array<double, 8> corner{};
    for (int i = 0; i < 8; ++i) {
        const int dx = i & 1;
        const int dy = (i >> 1) & 1;
        const int dz = (i >> 2) & 1;
        corner.at(static_cast<std::size_t>(i)) =
            grad3(hash3(ix + dx, iy + dy, iz + dz, seed), fx - dx, fy - dy, fz - dz);
    }
    const double u = fade(fx);
    const double v = fade(fy);
    const double w = fade(fz);
    const double x00 = lerp(corner[0], corner[1], u);
    const double x10 = lerp(corner[2], corner[3], u);
    const double x01 = lerp(corner[4], corner[5], u);
    const double x11 = lerp(corner[6], corner[7], u);
    const double value = lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
    return std::clamp(value, -1.0, 1.0);
}

}  // namespace reference

struct Nearest {
    float first = std::numeric_limits<float>::max();
    float second = std::numeric_limits<float>::max();
    int x = 0;
    int y = 0;
    int z = 0;
};

// Brute-force search over a window twice as wide as std.noise's own, using
// the module's published feature points.
Nearest reference_voronoi3(Vector3 p, int seed) {
    Nearest best;
    const int cx = static_cast<int>(std::floor(p.x));
    const int cy = static_cast<int>(std::floor(p.y));
    const int cz = static_cast<int>(std::floor(p.z));
    for (int x = cx - 4; x <= cx + 4; ++x) {
        for (int y = cy - 4; y <= cy + 4; ++y) {
            for (int z = cz - 4; z <= cz + 4; ++z) {
                const Vector3 f = std_noise__feature3(x, y, z, seed);
                const float d = std::sqrt(((f.x - p.x) * (f.x - p.x)) + ((f.y - p.y) * (f.y - p.y)) + ((f.z - p.z) * (f.z - p.z)));
                if (d < best.first) {
                    best.second = best.first;
                    best.first = d;
                    best.x = x;
                    best.y = y;
                    best.z = z;
                } else if (d < best.second) {
                    best.second = d;
                }
            }
        }
    }
    return best;
}

Nearest reference_voronoi2(Vector2 p, int seed) {
    Nearest best;
    const int cx = static_cast<int>(std::floor(p.x));
    const int cy = static_cast<int>(std::floor(p.y));
    for (int x = cx - 4; x <= cx + 4; ++x) {
        for (int y = cy - 4; y <= cy + 4; ++y) {
            const Vector2 f = std_noise__feature2(x, y, seed);
            const float d = std::sqrt(((f.x - p.x) * (f.x - p.x)) + ((f.y - p.y) * (f.y - p.y)));
            if (d < best.first) {
                best.second = best.first;
                best.first = d;
                best.x = x;
                best.y = y;
            } else if (d < best.second) {
                best.second = d;
            }
        }
    }
    return best;
}

// Deterministic sample points spread over several lattice cells and both signs.
Vector3 sample_point(int i) {
    const auto t = static_cast<float>(i);
    return Vector3{(t * 0.731F) - 40.0F, (t * -0.417F) + 13.0F, (t * 0.293F) - 7.5F};
}

const noise__Samples& rule_samples(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
    return registry.get<noise__Samples>(generated_named_slots().noise__Probe);
}

}  // namespace

// ── Perlin ──────────────────────────────────────────────────────────────────

TEST_CASE("perlin samples follow the documented recipe", "[runtime][codegen-entt][stdlib-noise]") {
    for (int i = 0; i < 400; ++i) {
        const Vector3 p = sample_point(i);
        CHECK(std_noise__perlin2(Vector2{p.x, p.y}, kSeed) == Catch::Approx(reference::perlin2(p.x, p.y, kSeed)).margin(kTolerance));
        CHECK(std_noise__perlin3(p, kSeed) == Catch::Approx(reference::perlin3(p.x, p.y, p.z, kSeed)).margin(kTolerance));
    }
}

TEST_CASE("perlin reference samples are pinned", "[runtime][codegen-entt][stdlib-noise]") {
    CHECK(std_noise__mix(1) == -127396752);
    CHECK(std_noise__hash3(2, -3, 4, kSeed) == -1554478858);
    CHECK(reference::hash2(0, 0, 0) == std_noise__hash2(0, 0, 0));
    CHECK(std_noise__perlin2(Vector2{3.7F, -12.25F}, kSeed) == Catch::Approx(0.4119584).margin(kTolerance));
    CHECK(std_noise__perlin3(Vector3{3.7F, -12.25F, 0.4F}, kSeed) == Catch::Approx(0.0644828).margin(kTolerance));
    CHECK(std_noise__perlin2(Vector2{0.5F, 0.5F}, 0) == Catch::Approx(0.3535534).margin(kTolerance));
    CHECK(std_noise__perlin3(Vector3{0.25F, 0.5F, 0.75F}, 0) == Catch::Approx(0.5013394).margin(kTolerance));
}

TEST_CASE("perlin values are normalized, varied and zero on lattice points", "[runtime][codegen-entt][stdlib-noise]") {
    float lowest = 0.0F;
    float highest = 0.0F;
    for (int i = 0; i < 2000; ++i) {
        const Vector3 p = sample_point(i);
        const float v2 = std_noise__perlin2(Vector2{p.x, p.y}, kSeed);
        const float v3 = std_noise__perlin3(p, kSeed);
        CHECK(v2 >= -1.0F);
        CHECK(v2 <= 1.0F);
        CHECK(v3 >= -1.0F);
        CHECK(v3 <= 1.0F);
        lowest = std::min({lowest, v2, v3});
        highest = std::max({highest, v2, v3});
    }
    CHECK(lowest < -0.4F);
    CHECK(highest > 0.4F);
    CHECK(std_noise__perlin2(Vector2{5.0F, -2.0F}, kSeed) == 0.0F);
    CHECK(std_noise__perlin3(Vector3{5.0F, -2.0F, 9.0F}, kSeed) == 0.0F);
}

TEST_CASE("perlin is continuous across lattice boundaries", "[runtime][codegen-entt][stdlib-noise]") {
    constexpr float kEpsilon = 1e-4F;
    for (int i = -3; i <= 3; ++i) {
        const auto edge = static_cast<float>(i);
        const float below = std_noise__perlin3(Vector3{edge - kEpsilon, 0.37F, 0.61F}, kSeed);
        const float above = std_noise__perlin3(Vector3{edge + kEpsilon, 0.37F, 0.61F}, kSeed);
        CHECK(std::abs(below - above) < 0.01F);
        const float left = std_noise__perlin2(Vector2{0.37F, edge - kEpsilon}, kSeed);
        const float right = std_noise__perlin2(Vector2{0.37F, edge + kEpsilon}, kSeed);
        CHECK(std::abs(left - right) < 0.01F);
    }
}

TEST_CASE("perlin depends on the seed but not on sampling order", "[runtime][codegen-entt][stdlib-noise]") {
    std::array<float, 64> forward{};
    for (int i = 0; i < 64; ++i) {
        forward.at(static_cast<std::size_t>(i)) = std_noise__perlin3(sample_point(i), kSeed);
    }
    int differing = 0;
    for (int i = 63; i >= 0; --i) {
        const Vector3 p = sample_point(i);
        CHECK(std_noise__perlin3(p, kSeed) == forward.at(static_cast<std::size_t>(i)));
        if (std::abs(std_noise__perlin3(p, kSeed + 1) - forward.at(static_cast<std::size_t>(i))) > 1e-3F) {
            ++differing;
        }
    }
    CHECK(differing > 48);
}

TEST_CASE("perlin returns zero for unsupported coordinates", "[runtime][codegen-entt][stdlib-noise]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(std_noise__perlin2(Vector2{nan, 0.5F}, kSeed) == 0.0F);
    CHECK(std_noise__perlin2(Vector2{0.5F, -inf}, kSeed) == 0.0F);
    CHECK(std_noise__perlin3(Vector3{0.5F, 0.5F, 1000001.0F}, kSeed) == 0.0F);
    CHECK(std_noise__perlin3(Vector3{-1000001.0F, 0.5F, 0.5F}, kSeed) == 0.0F);
    CHECK(std_noise__perlin3(Vector3{999999.5F, -999999.5F, 0.5F}, kSeed) == Catch::Approx(reference::perlin3(999999.5F, -999999.5F, 0.5F, kSeed)).margin(kTolerance));
}

// ── Voronoi ─────────────────────────────────────────────────────────────────

TEST_CASE("voronoi agrees with a wider reference search", "[runtime][codegen-entt][stdlib-noise]") {
    for (int i = 0; i < 300; ++i) {
        const Vector3 p = sample_point(i);
        const auto sample = std_noise__voronoi3(p, kSeed);
        const Nearest expected = reference_voronoi3(p, kSeed);
        REQUIRE(sample.valid);
        CHECK(sample.x == expected.x);
        CHECK(sample.y == expected.y);
        CHECK(sample.z == expected.z);
        CHECK(sample.nearest == Catch::Approx(expected.first).margin(kTolerance));
        CHECK(sample.second == Catch::Approx(expected.second).margin(kTolerance));
        CHECK(sample.nearest <= sample.second);

        const auto flat = std_noise__voronoi2(Vector2{p.x, p.y}, kSeed);
        const Nearest expected_flat = reference_voronoi2(Vector2{p.x, p.y}, kSeed);
        REQUIRE(flat.valid);
        CHECK(flat.x == expected_flat.x);
        CHECK(flat.y == expected_flat.y);
        CHECK(flat.z == 0);
        CHECK(flat.nearest == Catch::Approx(expected_flat.first).margin(kTolerance));
        CHECK(flat.second == Catch::Approx(expected_flat.second).margin(kTolerance));
    }
}

TEST_CASE("voronoi feature points stay inside their documented cell band", "[runtime][codegen-entt][stdlib-noise]") {
    for (int x = -6; x <= 6; ++x) {
        for (int y = -6; y <= 6; ++y) {
            const Vector3 f = std_noise__feature3(x, y, x - y, kSeed);
            CHECK(f.x - static_cast<float>(x) >= 0.05F);
            CHECK(f.x - static_cast<float>(x) < 0.95F);
            CHECK(f.y - static_cast<float>(y) >= 0.05F);
            CHECK(f.z - static_cast<float>(x - y) < 0.95F);
            const Vector2 g = std_noise__feature2(x, y, kSeed);
            CHECK(g.x - static_cast<float>(x) >= 0.05F);
            CHECK(g.y - static_cast<float>(y) < 0.95F);
        }
    }
}

TEST_CASE("voronoi distances are not normalized", "[runtime][codegen-entt][stdlib-noise]") {
    float widest = 0.0F;
    for (int i = 0; i < 500; ++i) {
        widest = std::max(widest, std_noise__voronoi3(sample_point(i), kSeed).second);
    }
    CHECK(widest > 1.0F);
}

TEST_CASE("voronoi ties choose the lexicographically smallest feature cell", "[runtime][codegen-entt][stdlib-noise]") {
    const Vector2 a = std_noise__feature2(0, 0, kSeed);
    const Vector2 b = std_noise__feature2(1, 0, kSeed);
    // The perpendicular bisector's midpoint is equidistant from both features;
    // only take the sample if no other feature is nearer.
    const Vector2 midpoint{(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F};
    const auto sample = std_noise__voronoi2(midpoint, kSeed);
    const Nearest expected = reference_voronoi2(midpoint, kSeed);
    if (expected.x == 0 || expected.x == 1) {
        if (std::abs(sample.nearest - sample.second) < 1e-6F) {
            CHECK(sample.x == 0);
        }
    }
    CHECK(std_noise__voronoi2(midpoint, kSeed).x == sample.x);
}

TEST_CASE("voronoi reports invalid samples for unsupported coordinates", "[runtime][codegen-entt][stdlib-noise]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = std_noise__voronoi3(Vector3{0.5F, nan, 0.5F}, kSeed);
    CHECK_FALSE(invalid.valid);
    CHECK(invalid.nearest == 0.0F);
    CHECK(invalid.second == 0.0F);
    CHECK(invalid.x == 0);
    CHECK(invalid.y == 0);
    CHECK(invalid.z == 0);
    CHECK_FALSE(std_noise__voronoi2(Vector2{2000000.0F, 0.0F}, kSeed).valid);
}

// ── Through ordinary rules ──────────────────────────────────────────────────

TEST_CASE("a rule samples noise with the same results as direct calls", "[runtime][codegen-entt][stdlib-noise]") {
    entt::registry registry;
    const auto& samples = rule_samples(registry);
    CHECK(samples.perlin2 == std_noise__perlin2(Vector2{3.7F, -12.25F}, kSeed));
    CHECK(samples.perlin3 == std_noise__perlin3(Vector3{3.7F, -12.25F, 0.4F}, kSeed));
    CHECK(samples.perlin_nan == 0.0F);
    CHECK(samples.perlin_infinite == 0.0F);
    CHECK(samples.perlin_far == 0.0F);
    CHECK(samples.voronoi2.valid);
    CHECK(samples.voronoi3.valid);
    CHECK(samples.voronoi3.x == std_noise__voronoi3(Vector3{3.7F, -12.25F, 0.4F}, kSeed).x);
    CHECK_FALSE(samples.voronoi_nan.valid);
    CHECK(samples.feature.x == std_noise__feature3(2, -3, 4, kSeed).x);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
