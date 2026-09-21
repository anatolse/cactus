#pragma once

#include "common/persistence_document.hpp"

#include <string>
#include <vector>

// Validates a decoded Snapshot against its program's SchemaDescriptor before
// restore touches the active world (world-persistence: "Documents are fully
// validated before the world changes"). Deliberately independent of any
// generated code or registry type: every check here reads only schema/
// snapshot data, so it is shared, plain, and unit-testable on its own.

namespace cactus::persistence {

// Distinguishes the two standard restore error codes validate_document can
// produce: a value whose kind or content is wrong (UnsupportedValue —
// "unsupported_value") versus a problem with the document's own structure —
// identities, archetypes, traits, hierarchy, limits (Structural —
// "invalid_data"). The caller reports "invalid_data" if any Structural error
// is present, else "unsupported_value" if only UnsupportedValue errors are —
// a document can carry both, and a structural problem takes priority since a
// value-content check may not even be meaningful until the structure itself
// is sound.
enum class ValidationErrorCategory : std::uint8_t { Structural, UnsupportedValue };

struct ValidationError {
    // "world_persistence.Boss[3].Health.current" — archetype, document id,
    // trait, and field, as far as the problem is located.
    std::string field_path;
    std::string message;
    ValidationErrorCategory category = ValidationErrorCategory::Structural;

    friend bool operator==(const ValidationError&, const ValidationError&) = default;
};

// Accumulates every problem found rather than stopping at the first one, so
// a caller can report them all together. An incompatible schema descriptor
// short-circuits every other check: field/trait/archetype tables may not
// even correspond, so nothing past that first error is safe to interpret.
[[nodiscard]] std::vector<ValidationError> validate_document(const SchemaDescriptor& schema, const Snapshot& snapshot);

}  // namespace cactus::persistence
