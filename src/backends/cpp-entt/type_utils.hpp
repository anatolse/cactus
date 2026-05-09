#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

class EnttCodegenUtils {
public:
    static std::string type_to_cpp(const TypeInfo& type);
    static std::string emit_enum(const ResolvedEnum& e);
    static std::string emit_expr(const ExprNode& expr, const ProgramNode* ast = nullptr);
};

}  // namespace cactus