#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cactus {

// Why one side of a weight has no bound: the source expression with no bound on that side.
struct UnboundedSource {
    enum class Kind : std::uint8_t { Other, PhaseStep, VarField };
    Kind kind = Kind::Other;
    std::string spelling;  // as written, such as `tick.dt` or `u.rate`
    std::string field;     // the field's name when `kind` is VarField
};

// One side of a weight: its bound, or the source that leaves it unbounded.
struct WeightBound {
    std::optional<double> value;
    UnboundedSource source;
};

// A write that moves a field toward a goal by a `float` weight, and the range of that weight.
struct BlendWeight {
    std::string rule;
    std::string field;  // the written field as spelled, such as `cam.offset`
    SourceLocation location;
    WeightBound lower;
    WeightBound upper;
};

// Every blend in the handlers of `program.ast`, with the derived range of its weight.
// Bounds describe every weight value that is not NaN. Empty when the program has no AST.
std::vector<BlendWeight> find_blend_weights(const DecoratedProgram& program);

// Warns about each blend whose weight is not proven to stay inside [0, 1].
void check_blend_weights(const DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
