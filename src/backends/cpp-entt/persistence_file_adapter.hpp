#pragma once

#include "backends/cpp-entt/runtime.hpp"

#include <filesystem>

// The documented example file adapter (add-world-save-restore section 5.3):
// one file per slot, encoded with a versioned, example-only binary format.
// The encoding is not a stability promise (design.md decision 3/7) — it
// exists to prove an adapter can be built entirely from the generated schema
// descriptor and typed snapshot, and to give an author who writes only
// Cactus somewhere to save without writing host C++ (task 5.4 registers this
// adapter by default in generated main()).
//
// Writes are atomic: the document is written to a temporary file first and
// only renamed over the slot's real file once fully written, so a failed or
// interrupted write never corrupts the previously committed save.

namespace cactus::runtime::entt_backend {

[[nodiscard]] PersistenceAdapter make_example_file_adapter(const std::filesystem::path& directory);

}  // namespace cactus::runtime::entt_backend
