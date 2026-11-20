#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

// The trait a write names: by canonical id, else by local name.
[[nodiscard]] const ResolvedTrait* find_written_trait(const DecoratedProgram& program, const SymbolId& trait);

// The field `trait.field` when it declares `clamped`, else null.
[[nodiscard]] const ResolvedField* find_clamped_field(const DecoratedProgram& program,
                                                      const SymbolId& trait,
                                                      const std::string& field);

// The clamped field `assign` writes, else null.
[[nodiscard]] const ResolvedField* clamped_write_target(const VarAssign& assign, const DecoratedProgram& program);

// The written field `lhs`, the assigned `value` and the owning `entity` of a write to a clamped field.
struct ClampedWrite {
    std::string lhs;
    std::string value;
    std::string entity;
    std::string indent;
};

// The C++ for `assign`, which writes clamped `field`: the stored value always ends inside the bounds.
[[nodiscard]] std::string emit_clamped_write(const VarAssign& assign,
                                             const ResolvedField& field,
                                             const ClampedWrite& write,
                                             const DecoratedProgram& program);

// Checks of the clamped fields of `trait` for its value `value`, each `return`ing a RangeFault:
// crossed `let` bounds always, and a value outside its bounds when `check_clamped` is true.
[[nodiscard]] std::string emit_clamp_checks(const ResolvedTrait& trait,
                                            const std::string& value,
                                            const DecoratedProgram& program);
[[nodiscard]] bool has_clamped_field(const ResolvedTrait& trait);

// Statements moving every clamped field of the trait value `object` into its bounds; empty when it has none.
// For values written in place, which no storage listener sees.
[[nodiscard]] std::string emit_clamp_projections(const ResolvedTrait& trait,
                                                 const std::string& object,
                                                 const std::string& indent,
                                                 const DecoratedProgram& program);

// A storage listener per trait with a clamped field, which moves a made or replaced value into its
// bounds, and `generated_connect_clamped_fields`. Empty when no field is clamped.
[[nodiscard]] std::string emit_clamp_listeners(const DecoratedProgram& program);
[[nodiscard]] bool program_has_clamped_fields(const DecoratedProgram& program);

}  // namespace cactus
