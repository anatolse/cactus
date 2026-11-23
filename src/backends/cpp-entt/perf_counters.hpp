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
    std::uint64_t link_updates     = 0;  // holders a link index added or dropped
    std::uint64_t lost_links       = 0;  // holders marked because their link target was lost
    std::uint64_t keep_runs        = 0;  // keep rule passes run
    std::uint64_t commit_rounds    = 0;  // commit rounds that applied commands or ran keep rules
    std::uint64_t drawing_rows     = 0;  // domain rows a drawing rule's draw loop visited
    std::uint64_t drawing_quads    = 0;  // quads a drawing rule drew
    std::uint64_t drawing_uniforms = 0;  // shader values a drawing rule set
    std::uint64_t repeat_passes    = 0;  // passes run by rules with `repeat:`
    std::uint64_t cell_rows        = 0;  // left cells a rule over cells visited
    std::uint64_t clamp_merges     = 0;  // deltas merged into a clamped field's exact pending value
    std::uint64_t clamp_settles    = 0;  // clamped pending values settled when an activation ended
    std::uint64_t component_moves  = 0;  // trait components constructed or destroyed in the registry
    std::uint64_t domain_rows      = 0;  // rows a unary rule's pass visited, before its row tests
    std::uint64_t color_conversions = 0;  // colors converted to 8-bit channels for a raylib call
    std::uint64_t propagated_nodes  = 0;  // entities a transform propagation looked at

    PerfCounters& operator+=(const PerfCounters& other) noexcept {
        commands += other.commands;
        orderings += other.orderings;
        candidates += other.candidates;
        carrier_lookups += other.carrier_lookups;
        parallel_batches += other.parallel_batches;
        parallel_tasks += other.parallel_tasks;
        link_updates += other.link_updates;
        lost_links += other.lost_links;
        keep_runs += other.keep_runs;
        commit_rounds += other.commit_rounds;
        drawing_rows += other.drawing_rows;
        drawing_quads += other.drawing_quads;
        drawing_uniforms += other.drawing_uniforms;
        repeat_passes += other.repeat_passes;
        cell_rows += other.cell_rows;
        clamp_merges += other.clamp_merges;
        clamp_settles += other.clamp_settles;
        component_moves += other.component_moves;
        domain_rows += other.domain_rows;
        color_conversions += other.color_conversions;
        propagated_nodes += other.propagated_nodes;
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

inline void count_link_update() noexcept {
    ++counting_target().link_updates;
}

inline void count_lost_link() noexcept {
    ++counting_target().lost_links;
}

inline void count_keep_run() noexcept {
    ++counting_target().keep_runs;
}

inline void count_commit_round() noexcept {
    ++counting_target().commit_rounds;
}

inline void count_drawing_row() noexcept {
    ++counting_target().drawing_rows;
}

inline void count_drawing_quad() noexcept {
    ++counting_target().drawing_quads;
}

inline void count_drawing_uniform() noexcept {
    ++counting_target().drawing_uniforms;
}

inline void count_cell_row() noexcept {
    ++counting_target().cell_rows;
}

inline void count_repeat_pass() noexcept {
    ++counting_target().repeat_passes;
}

inline void count_clamp_work(std::size_t merges, std::size_t settles) noexcept {
    counting_target().clamp_merges += merges;
    counting_target().clamp_settles += settles;
}

inline void count_component_move() noexcept {
    ++counting_target().component_moves;
}

inline void count_domain_row() noexcept {
    ++counting_target().domain_rows;
}

inline void count_color_conversion() noexcept {
    ++counting_target().color_conversions;
}

inline void count_propagated_node() noexcept {
    ++counting_target().propagated_nodes;
}
#else
inline void count_commands(std::size_t /*count*/) noexcept {}
inline void count_ordering() noexcept {}
inline void count_candidate() noexcept {}
inline void count_carrier_lookup() noexcept {}
inline void count_parallel_batch(std::size_t /*tasks*/) noexcept {}
inline void count_link_update() noexcept {}
inline void count_lost_link() noexcept {}
inline void count_keep_run() noexcept {}
inline void count_commit_round() noexcept {}
inline void count_drawing_row() noexcept {}
inline void count_cell_row() noexcept {}
inline void count_drawing_quad() noexcept {}
inline void count_drawing_uniform() noexcept {}
inline void count_repeat_pass() noexcept {}
inline void count_clamp_work(std::size_t /*merges*/, std::size_t /*settles*/) noexcept {}
inline void count_component_move() noexcept {}
inline void count_domain_row() noexcept {}
inline void count_color_conversion() noexcept {}
inline void count_propagated_node() noexcept {}
#endif

}  // namespace cactus::runtime::entt_backend
