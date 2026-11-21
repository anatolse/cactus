#pragma once

#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// Resolves each `SelfCommands` pass to `Free` when no part of the linked program can
// see the order of its own-row `add` / `remove` commands, and to `Creation` otherwise.
void resolve_self_command_order(DecoratedProgram& program);

}  // namespace cactus
