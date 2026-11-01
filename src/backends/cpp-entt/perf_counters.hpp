#pragma once

#include <cstddef>
#include <cstdint>

namespace cactus::runtime::entt_backend {

// Work counts that performance regression tests check. Only headless test
// builds count; shipped games compile every call below to nothing.
struct PerfCounters {
    std::uint64_t commands   = 0;  // structural commands applied at commit
    std::uint64_t orderings  = 0;  // binding memberships sorted into creation order
    std::uint64_t candidates = 0;  // tuples a pair pass, fold or row join checked
};

#ifdef CACTUS_RAYLIB_FAKE
inline PerfCounters& perf_counters() noexcept {
    static PerfCounters counters;
    return counters;
}

inline void count_commands(std::size_t count) noexcept {
    perf_counters().commands += count;
}

inline void count_ordering() noexcept {
    ++perf_counters().orderings;
}

inline void count_candidate() noexcept {
    ++perf_counters().candidates;
}
#else
inline void count_commands(std::size_t /*count*/) noexcept {}
inline void count_ordering() noexcept {}
inline void count_candidate() noexcept {}
#endif

}  // namespace cactus::runtime::entt_backend
