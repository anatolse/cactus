#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cactus {

// What one stdlib native function creates when a handler calls it.
struct NativeCreation {
    bool pub_templates = false;                        // any `pub template` of the program
    std::vector<std::vector<std::string>> trait_sets;  // canonical trait ids, one set per entity kind
};

// Null unless the function, by canonical id, creates entities.
[[nodiscard]] const NativeCreation* native_creation(std::string_view function);

struct ReachabilityOptions {
    // Past this many abstract entities, the trait that splits the most is made free.
    std::size_t widening_bound = 4096;
};

// Computes which trait sets entities of the whole program can carry, stores
// them in `program.reachability`, and reports declarations that can never act.
// Then derives `program.hierarchy` from the reachable handlers.
void derive_archetype_reachability(DecoratedProgram& program,
                                   ErrorReporter& errors,
                                   const ReachabilityOptions& options = {});

}  // namespace cactus
