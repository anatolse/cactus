#pragma once

#include "frontend/symbol_identity.hpp"

namespace cactus::core_events {

/// The module declaring the lifecycle events the scheduler itself produces.
inline constexpr const char* MODULE = "std.core";

/// Canonical identities the scheduler recognizes as runtime-owned triggers.
/// Named here once so producer recognition and any later backend migration
/// resolve them from the same place instead of re-spelling them.
inline SymbolId load() {
    return make_symbol_id(SymbolKind::Event, MODULE, "load");
}

inline SymbolId unload() {
    return make_symbol_id(SymbolKind::Event, MODULE, "unload");
}

inline SymbolId spawn() {
    return make_symbol_id(SymbolKind::Event, MODULE, "spawn");
}

inline SymbolId destroy() {
    return make_symbol_id(SymbolKind::Event, MODULE, "destroy");
}

/// True when `event` is a scheduler boot/teardown boundary event.
inline bool is_scheduler_boundary(const SymbolId& event) {
    return event == load() || event == unload();
}

/// True when `event` is synthesized by the structural-command commit step.
inline bool is_activation_commit(const SymbolId& event) {
    return event == spawn() || event == destroy();
}

}  // namespace cactus::core_events
