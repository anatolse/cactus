#pragma once

#include <cstddef>
#include <cstdint>

namespace cactus::runtime::entt_backend {

// Work counts that performance regression tests check. Only headless test
// builds count; shipped games compile every call below to nothing.
struct PerfCounters {
    std::uint64_t commands         = 0;  // structural commands applied at commit
    std::uint64_t orderings        = 0;  // binding memberships sorted into creation order
    std::uint64_t candidates       = 0;  // tuples a pair pass, fold or row join checked
    std::uint64_t carrier_lookups  = 0;  // single-trait carriers found at the start of a commit round
    std::uint64_t parallel_batches = 0;  // batches that handed handlers to the worker pool
    std::uint64_t parallel_tasks   = 0;  // handlers handed to the worker pool

    PerfCounters& operator+=(const PerfCounters& other) noexcept {
        commands += other.commands;
        orderings += other.orderings;
        candidates += other.candidates;
        carrier_lookups += other.carrier_lookups;
        parallel_batches += other.parallel_batches;
        parallel_tasks += other.parallel_tasks;
        return *this;
    }
};

#ifdef CACTUS_RAYLIB_FAKE
inline PerfCounters& perf_counters() noexcept {
    static PerfCounters counters;
    return counters;
}

// While a parallel handler runs, its counts go to its own counters, added to perf_counters() at the join.
inline PerfCounters*& task_perf_counters() noexcept {
    thread_local PerfCounters* counters = nullptr;
    return counters;
}

inline PerfCounters& counting_target() noexcept {
    auto* task = task_perf_counters();
    return task != nullptr ? *task : perf_counters();
}

inline void count_commands(std::size_t count) noexcept {
    counting_target().commands += count;
}

inline void count_ordering() noexcept {
    ++counting_target().orderings;
}

inline void count_candidate() noexcept {
    ++counting_target().candidates;
}

inline void count_carrier_lookup() noexcept {
    ++counting_target().carrier_lookups;
}

inline void count_parallel_batch(std::size_t tasks) noexcept {
    ++perf_counters().parallel_batches;
    perf_counters().parallel_tasks += tasks;
}
#else
inline void count_commands(std::size_t /*count*/) noexcept {}
inline void count_ordering() noexcept {}
inline void count_candidate() noexcept {}
inline void count_carrier_lookup() noexcept {}
inline void count_parallel_batch(std::size_t /*tasks*/) noexcept {}
#endif

}  // namespace cactus::runtime::entt_backend
