#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace cactus {

// Every link field of `traits`, sorted by marker.
[[nodiscard]] std::vector<LinkField> link_fields(const std::unordered_map<std::string, ResolvedTrait>& traits);

// Whether an `on lost` body leaves the lost state on every path: it swaps `link.holder`'s
// variant, gives a new target with `add`, removes the holder trait or its state, or destroys
// the holder. An `if` counts only with an `else`, and a loop never does.
[[nodiscard]] bool resolves_on_every_path(const std::vector<std::unique_ptr<StmtNode>>& body, const LostLink& link);

// Sets each handler's `lost_markers` to the links one of its commands may lose:
// a destroy, a `remove` of the target trait, or a swap away from a target variant.
// Run before scheduling, so lifecycle flows reach the `on lost` handlers.
void derive_link_losses(std::vector<HandlerNode>& handlers, const std::unordered_map<std::string, ResolvedTrait>& traits);

// Rejects links to or on projected traits, and every link the reachable program can
// lose whose reachable holders are not all covered by an `on lost` handler. Records
// the links it can lose in `program.losable_links` for the backend.
void validate_required_links(DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
