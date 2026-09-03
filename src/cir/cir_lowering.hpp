#pragma once

#include "frontend/semantic_analyzer.hpp"

#include "cir/cir.hpp"

namespace cactus::cir {

/// Lowers a program that completed semantic analysis — and, when imports are
/// present, program linking — into CIR v1.
///
/// Authoritative execution records (phases, handlers, schedule edges, barriers,
/// event flows, render-pass plans, stable order, dependency levels) are copied,
/// never recomputed. The only derived information is the per-edge access
/// provenance the graph does not itself store. Every unordered input is
/// normalized into canonical vector order here, so nothing downstream can
/// inherit hash iteration order.
[[nodiscard]] CirProgram lower_program(const DecoratedProgram& program);

}  // namespace cactus::cir
