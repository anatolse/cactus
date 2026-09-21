#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

// Generated world reconstruction: the read-side counterpart to
// EnttPersistenceEmitter. Reuses its schema/archetype-node table and its
// persistable_traits() listing so capture and restore can never disagree on
// which traits or archetypes participate in persistence.
class EnttRestoreEmitter {
public:
    // The generated restore entry point: allocates a fresh handle for every
    // recorded entity, then reconstructs each node's traits, parent link, and
    // capture provenance from its record. Empty when the program never
    // retains construction data (mirrors emit_world_capture; nothing can be
    // restored into a program that could never have captured it).
    [[nodiscard]] static std::string emit_world_restore(const DecoratedProgram& program);
};

}  // namespace cactus
