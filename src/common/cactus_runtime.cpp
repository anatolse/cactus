#include "common/cactus_runtime.h"

#include <algorithm>
#include <cmath>

#include <raymath.h>

namespace cactus::runtime {

namespace {
[[maybe_unused]] constexpr auto kCommonRuntimeName = common_runtime_name();

[[nodiscard]] constexpr float length_squared(Vector2 v) noexcept {
    return (v.x * v.x) + (v.y * v.y);
}

[[nodiscard]] constexpr float length_squared(Vector3 v) noexcept {
    return (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
}
}  // namespace

namespace stdlib::math {

float sqrt(float v) noexcept {
    return std::sqrt(v);
}

float sin(float a) noexcept {
    return std::sin(a);
}

float cos(float a) noexcept {
    return std::cos(a);
}

float atan2(float y, float x) noexcept {
    return std::atan2(y, x);
}

int floor(float v) noexcept {
    return static_cast<int>(std::floor(v));
}

int ceil(float v) noexcept {
    return static_cast<int>(std::ceil(v));
}

int round(float v) noexcept {
    return static_cast<int>(std::round(v));
}

float pow(float base, float exp) noexcept {
    return std::pow(base, exp);
}

namespace vec2 {

float length(Vector2 v) noexcept {
    return std::sqrt(length_squared(v));
}

Vector2 normalize(Vector2 v) noexcept {
    const float len_sq = length_squared(v);
    if (len_sq <= 0.0F) {
        return Vector2{0.0F, 0.0F};
    }
    const float inv_len = 1.0F / std::sqrt(len_sq);
    return Vector2{v.x * inv_len, v.y * inv_len};
}

float distance(Vector2 a, Vector2 b) noexcept {
    return length(Vector2{a.x - b.x, a.y - b.y});
}

float angle(Vector2 v) noexcept {
    return std::atan2(v.y, v.x);
}

}  // namespace vec2

namespace vec3 {

float length(Vector3 v) noexcept {
    return std::sqrt(length_squared(v));
}

Vector3 normalize(Vector3 v) noexcept {
    const float len_sq = length_squared(v);
    if (len_sq <= 0.0F) {
        return Vector3{0.0F, 0.0F, 0.0F};
    }
    const float inv_len = 1.0F / std::sqrt(len_sq);
    return Vector3{v.x * inv_len, v.y * inv_len, v.z * inv_len};
}

float distance(Vector3 a, Vector3 b) noexcept {
    return length(Vector3{a.x - b.x, a.y - b.y, a.z - b.z});
}

}  // namespace vec3

namespace quat {

Quat from_euler(float pitch, float yaw, float roll) noexcept {
    return QuaternionFromEuler(pitch, yaw, roll);
}

Quat from_axis_angle(Vector3 axis, float angle) noexcept {
    return QuaternionFromAxisAngle(axis, angle);
}

Vector3 forward(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{0.0F, 0.0F, -1.0F}, q);
}

Vector3 right(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{1.0F, 0.0F, 0.0F}, q);
}

Vector3 up(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{0.0F, 1.0F, 0.0F}, q);
}

Vector3 rotate(Quat q, Vector3 v) noexcept {
    return Vector3RotateByQuaternion(v, q);
}

Quat slerp(Quat a, Quat b, float t) noexcept {
    return QuaternionSlerp(a, b, t);
}

Quat inverse(Quat q) noexcept {
    return QuaternionInvert(q);
}

}  // namespace quat

}  // namespace stdlib::math

}  // namespace cactus::runtime