#pragma once

#include <raylib.h>

#include <string_view>

namespace cactus::runtime {

using Quat = Quaternion;

struct GeneratedProjectInfo {
    std::string_view backend;
    std::string_view project_name;
};

[[nodiscard]] constexpr std::string_view common_runtime_name() noexcept {
    return "cactus-runtime-common";
}

[[nodiscard]] constexpr GeneratedProjectInfo make_project_info(std::string_view backend,
                                                               std::string_view project_name) noexcept {
    return GeneratedProjectInfo{backend, project_name};
}

namespace stdlib::math {

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept;
[[nodiscard]] constexpr float clamp(float x, float lo, float hi) noexcept;
[[nodiscard]] constexpr float abs(float v) noexcept;
[[nodiscard]] constexpr float min(float a, float b) noexcept;
[[nodiscard]] constexpr float max(float a, float b) noexcept;
[[nodiscard]] float sqrt(float v) noexcept;
[[nodiscard]] float sin(float a) noexcept;
[[nodiscard]] float cos(float a) noexcept;
[[nodiscard]] float atan2(float y, float x) noexcept;
[[nodiscard]] int floor(float v) noexcept;
[[nodiscard]] int ceil(float v) noexcept;
[[nodiscard]] int round(float v) noexcept;
[[nodiscard]] float pow(float base, float exp) noexcept;

namespace vec2 {
[[nodiscard]] float length(Vector2 v) noexcept;
[[nodiscard]] Vector2 normalize(Vector2 v) noexcept;
[[nodiscard]] constexpr float dot(Vector2 a, Vector2 b) noexcept;
[[nodiscard]] constexpr Vector2 lerp(Vector2 a, Vector2 b, float t) noexcept;
[[nodiscard]] float distance(Vector2 a, Vector2 b) noexcept;
[[nodiscard]] float angle(Vector2 v) noexcept;
}  // namespace vec2

namespace vec3 {
[[nodiscard]] float length(Vector3 v) noexcept;
[[nodiscard]] Vector3 normalize(Vector3 v) noexcept;
[[nodiscard]] constexpr float dot(Vector3 a, Vector3 b) noexcept;
[[nodiscard]] constexpr Vector3 cross(Vector3 a, Vector3 b) noexcept;
[[nodiscard]] constexpr Vector3 lerp(Vector3 a, Vector3 b, float t) noexcept;
[[nodiscard]] float distance(Vector3 a, Vector3 b) noexcept;
[[nodiscard]] constexpr Vector3 reflect(Vector3 v, Vector3 normal) noexcept;
}  // namespace vec3

namespace quat {
[[nodiscard]] constexpr Quat identity() noexcept;
[[nodiscard]] Quat from_euler(float pitch, float yaw, float roll) noexcept;
[[nodiscard]] Quat from_axis_angle(Vector3 axis, float angle) noexcept;
[[nodiscard]] Vector3 forward(Quat q) noexcept;
[[nodiscard]] Vector3 right(Quat q) noexcept;
[[nodiscard]] Vector3 up(Quat q) noexcept;
[[nodiscard]] Vector3 rotate(Quat q, Vector3 v) noexcept;
[[nodiscard]] Quat slerp(Quat a, Quat b, float t) noexcept;
[[nodiscard]] constexpr Quat multiply(Quat a, Quat b) noexcept;
[[nodiscard]] Quat inverse(Quat q) noexcept;
}  // namespace quat

}  // namespace stdlib::math

}  // namespace cactus::runtime