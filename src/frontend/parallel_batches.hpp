#pragma once

#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// A phase handler of a Cactus rule with no effect, spawn, projection, call to
// a host facade such as `input.pressed`, or lookup of a relation whose rows
// are computed per key on first use.
[[nodiscard]] bool parallel_eligible(const HandlerNode& handler, const DecoratedProgram& program);

[[nodiscard]] ParallelPlan plan_parallel_handlers(const DecoratedProgram& program);

}  // namespace cactus
