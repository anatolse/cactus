#pragma once

#include "cir/cir.hpp"

#include <string>

namespace cactus::cir {

/// Serializes CIR v1 as its complete, versioned interchange form: UTF-8, two
/// space indentation, fixed member order, and a trailing newline. Serializing
/// the same `CirProgram` twice is byte-for-byte identical.
[[nodiscard]] std::string write_json(const CirProgram& cir);

}  // namespace cactus::cir
