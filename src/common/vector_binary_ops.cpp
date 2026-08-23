#include "common/vector_binary_ops.hpp"

#include <array>

namespace cactus {

std::optional<TypeKind> lookup_vector_binary_op_result(TypeKind left, const std::string& op, TypeKind right) {
    struct Row {
        TypeKind left;
        const char* op;
        TypeKind right;
        TypeKind result;
    };
    static constexpr std::array<Row, 18> kRows{{
        {.left = TypeKind::Vec2, .op = "+", .right = TypeKind::Vec2, .result = TypeKind::Vec2},
        {.left = TypeKind::Vec3, .op = "+", .right = TypeKind::Vec3, .result = TypeKind::Vec3},
        {.left = TypeKind::Vec2, .op = "-", .right = TypeKind::Vec2, .result = TypeKind::Vec2},
        {.left = TypeKind::Vec3, .op = "-", .right = TypeKind::Vec3, .result = TypeKind::Vec3},
        {.left = TypeKind::Vec2, .op = "*", .right = TypeKind::Float, .result = TypeKind::Vec2},
        {.left = TypeKind::Float, .op = "*", .right = TypeKind::Vec2, .result = TypeKind::Vec2},
        {.left = TypeKind::Vec3, .op = "*", .right = TypeKind::Float, .result = TypeKind::Vec3},
        {.left = TypeKind::Float, .op = "*", .right = TypeKind::Vec3, .result = TypeKind::Vec3},
        {.left = TypeKind::Vec2, .op = "/", .right = TypeKind::Float, .result = TypeKind::Vec2},
        {.left = TypeKind::Vec3, .op = "/", .right = TypeKind::Float, .result = TypeKind::Vec3},
        {.left = TypeKind::Vec2, .op = "*", .right = TypeKind::Vec2, .result = TypeKind::Vec2},
        {.left = TypeKind::Vec3, .op = "*", .right = TypeKind::Vec3, .result = TypeKind::Vec3},
        {.left = TypeKind::Color, .op = "+", .right = TypeKind::Color, .result = TypeKind::Color},
        {.left = TypeKind::Color, .op = "-", .right = TypeKind::Color, .result = TypeKind::Color},
        {.left = TypeKind::Color, .op = "*", .right = TypeKind::Float, .result = TypeKind::Color},
        {.left = TypeKind::Float, .op = "*", .right = TypeKind::Color, .result = TypeKind::Color},
        {.left = TypeKind::Color, .op = "/", .right = TypeKind::Float, .result = TypeKind::Color},
        {.left = TypeKind::Color, .op = "*", .right = TypeKind::Color, .result = TypeKind::Color},
    }};
    for (const auto& row : kRows) {
        if (row.left == left && row.right == right && op == row.op) {
            return row.result;
        }
    }
    return std::nullopt;
}

}  // namespace cactus
