#pragma once

#include "backends/cpp-manual/soa_emitter.h"
#include "frontend/ast.h"
#include "frontend/semantic_analyzer.h"

#include <string>

namespace cactus {

class ManualSystemEmitter {
public:
    static std::string emit_system(const SystemNode& sys, const DecoratedProgram& program);

    static std::string emit_stmt(const StmtNode& stmt, int indent);
    static std::string emit_expr(const ExprNode& expr);
    static std::string indent_str(int level);
};

}  // namespace cactus
