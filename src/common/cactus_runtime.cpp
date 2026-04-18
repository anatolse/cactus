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

constexpr float lerp(float a, float b, float t) noexcept {
    return a + ((b - a) * t);
}

constexpr float clamp(float x, float lo, float hi) noexcept {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

constexpr float abs(float v) noexcept {
    return (v < 0.0F) ? -v : v;
}

constexpr float min(float a, float b) noexcept {
    return (a < b) ? a : b;
}

constexpr float max(float a, float b) noexcept {
    return (a > b) ? a : b;
}

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

constexpr float dot(Vector2 a, Vector2 b) noexcept {
    return (a.x * b.x) + (a.y * b.y);
}

constexpr Vector2 lerp(Vector2 a, Vector2 b, float t) noexcept {
    return Vector2{
        .x = stdlib::math::lerp(a.x, b.x, t),
        .y = stdlib::math::lerp(a.y, b.y, t),
    };
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

constexpr float dot(Vector3 a, Vector3 b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

constexpr Vector3 cross(Vector3 a, Vector3 b) noexcept {
    return Vector3{
        .x = (a.y * b.z) - (a.z * b.y),
        .y = (a.z * b.x) - (a.x * b.z),
        .z = (a.x * b.y) - (a.y * b.x),
    };
}

constexpr Vector3 lerp(Vector3 a, Vector3 b, float t) noexcept {
    return Vector3{
        .x = stdlib::math::lerp(a.x, b.x, t),
        .y = stdlib::math::lerp(a.y, b.y, t),
        .z = stdlib::math::lerp(a.z, b.z, t),
    };
}

float distance(Vector3 a, Vector3 b) noexcept {
    return length(Vector3{a.x - b.x, a.y - b.y, a.z - b.z});
}

constexpr Vector3 reflect(Vector3 v, Vector3 normal) noexcept {
    const float scale = 2.0F * dot(v, normal);
    return Vector3{
        .x = v.x - (scale * normal.x),
        .y = v.y - (scale * normal.y),
        .z = v.z - (scale * normal.z),
    };
}

}  // namespace vec3

namespace quat {

constexpr Quat identity() noexcept {
    return Quat{0.0F, 0.0F, 0.0F, 1.0F};
}

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

constexpr Quat multiply(Quat a, Quat b) noexcept {
    return Quat{
        .x = (a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y),
        .y = (a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
        .z = (a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w),
        .w = (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z),
    };
}

Quat inverse(Quat q) noexcept {
    return QuaternionInvert(q);
}

}  // namespace quat

}  // namespace stdlib::math

}  // namespace cactus::runtime