#pragma once

// Shared helpers for the generated-code performance benchmarks. Include after
// the generated translation unit, like fake_raylib/headless_frame_driver.hpp.
//
// Work counts are deterministic and checked in every build. Times are checked
// only in optimized, uninstrumented builds: absolute limits scale by
// CACTUS_PERF_SCALE, growth ratios never do.

#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace cactus_perf {

namespace backend = cactus::runtime::entt_backend;

constexpr int kWarmupFrames   = 5;
constexpr int kMeasuredFrames = 41;

[[nodiscard]] constexpr bool timing_enabled() {
#if defined(NDEBUG) && !defined(CACTUS_PERF_COVERAGE)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] inline double time_scale() {
    std::string value;
#ifdef _WIN32
    char* buffer     = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, "CACTUS_PERF_SCALE") == 0 && buffer != nullptr) {
        value = buffer;
    }
    std::free(buffer);  // NOLINT(cppcoreguidelines-no-malloc) -- _dupenv_s allocates with malloc
#else
    if (const char* found = std::getenv("CACTUS_PERF_SCALE")) {  // NOLINT(concurrency-mt-unsafe) -- read before frames run
        value = found;
    }
#endif
    const double scale = value.empty() ? 1.0 : std::strtod(value.c_str(), nullptr);
    return scale > 0.0 ? scale : 1.0;
}

// What one call of `step` did, by the runtime's work counters.
template <typename Step>
[[nodiscard]] backend::PerfCounters work_of(Step&& step) {
    const auto before = backend::perf_counters();
    std::forward<Step>(step)();
    const auto& after = backend::perf_counters();
    return {.commands   = after.commands - before.commands,
                .orderings  = after.orderings - before.orderings,
                .candidates = after.candidates - before.candidates};
}

// Median milliseconds per call of `step`, after a warm-up.
template <typename Step>
[[nodiscard]] double median_ms(Step& step, int warmup = kWarmupFrames, int frames = kMeasuredFrames) {
    for (int frame = 0; frame < warmup; ++frame) {
        step();
    }
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        step();
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::ranges::sort(samples);
    return samples[samples.size() / 2];
}

struct TimeLimits {
    double base_ms = 0.0;  // at size N, before CACTUS_PERF_SCALE
    double growth  = 0.0;  // t(2N) / t(N)
};

inline void skip_unless_timed() {
    if (!timing_enabled()) {
        SKIP("time limits run only in optimized builds without coverage");
    }
}

// Checks the median frame of `base_step` (size N) and `double_step` (size 2N) against `limits`.
template <typename BaseStep, typename DoubleStep>
void require_frame_times(BaseStep&& base_step, DoubleStep&& double_step, const TimeLimits& limits) {
    const double base_ms   = median_ms(base_step);
    const double double_ms = median_ms(double_step);
    INFO("t(N) = " << base_ms << " ms, t(2N) = " << double_ms << " ms, ratio = " << double_ms / base_ms);
    CHECK(base_ms <= limits.base_ms * time_scale());
    CHECK(double_ms / base_ms <= limits.growth);
}

// Entities of view `T...`, oldest first.
template <typename... T>
[[nodiscard]] std::vector<entt::entity> oldest_first(entt::registry& registry) {
    return backend::creation_ordered(registry, registry.view<T...>());
}

// Brings a registry to its first frame with `set_size` applied between the
// project's load and its `on load` handlers, so the load spawns that size.
template <typename SetSize>
void load_sized(entt::registry& registry, SetSize&& set_size) {
    backend::generated_init_project(registry);
    backend::generated_load_project(registry);
    std::forward<SetSize>(set_size)(registry);
    cactus_headless_test::dispatch_load_event(registry);
}

}  // namespace cactus_perf
