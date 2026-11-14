#pragma once

// Codegen for --contract-audit: the contract tables, and the checks generated
// code makes against them (backends/cpp-entt/contract_audit.hpp).

#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus::contract_audit {

// Names every program trait with fields for the storage hook. Must follow the
// component structs and precede any code that touches their storage.
[[nodiscard]] std::string emit_trait_names(const DecoratedProgram& program);

// Registers every handler's contract and every summarized native's traits.
[[nodiscard]] std::string emit_tables(const DecoratedProgram& program);

// The first line of a handler function.
[[nodiscard]] std::string emit_handler_scope(const HandlerIdentity& identity);

// The checks one statement makes before it runs; empty for most statements.
[[nodiscard]] std::string statement_checks(const StmtNode& stmt, const std::string& indent, const DecoratedProgram& program);

// `lowered` with the checks its expression makes: a field read, the effects of
// a call, and the native-call scope of a summarized native.
[[nodiscard]] std::string audited_expr(const ExprNode& expr, std::string lowered, const DecoratedProgram& program);

}  // namespace cactus::contract_audit
