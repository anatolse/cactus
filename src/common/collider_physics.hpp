#pragma once

#include <raylib.h>

#include <cstdint>
#include <optional>

// Shape-agnostic 3D collider queries over plain data. Every shape is a convex
// core (point, vertical segment or oriented box) plus a radius, so one GJK
// distance routine serves all nine shape pairs.
namespace cactus::runtime::physics {

enum class ShapeKind : std::uint8_t { Box, Sphere, Capsule };

struct ColliderShape {
    ShapeKind kind{ShapeKind::Sphere};
    Vector3 center{};
    Quaternion rotation{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    Vector3 half_extents{};  // box core
    float radius{0.0F};
    float half_segment{0.0F};  // capsule core: half length along world Y
    int layer{1};
    int mask{1};
};

[[nodiscard]] ColliderShape box_shape(Vector3 center, Quaternion rotation, Vector3 size, int layer, int mask) noexcept;
[[nodiscard]] ColliderShape sphere_shape(Vector3 center, float radius, int layer, int mask) noexcept;
[[nodiscard]] ColliderShape capsule_shape(Vector3 center, float radius, float height, int layer, int mask) noexcept;

inline constexpr float kSkin = 1e-4F;

// Closest points between two cores, radii ignored.
struct CoreDistance {
    float distance{0.0F};
    Vector3 on_a{};
    Vector3 on_b{};
    bool overlap{false};
};

[[nodiscard]] CoreDistance core_distance(const ColliderShape& a, const ColliderShape& b) noexcept;

// Signed gap between the shapes: negative when they overlap. `normal` is unit
// length and points from `b` toward `a`; `point` lies on `b`'s surface.
struct Separation {
    float distance{0.0F};
    Vector3 normal{};
    Vector3 point{};
};

[[nodiscard]] Separation separation(const ColliderShape& a, const ColliderShape& b) noexcept;

// `normal` points from the target toward the subject at the contact. `surface`
// is the target's surface normal there: on a box edge or corner, the face the
// motion meets first.
struct SweepResult {
    bool hit{false};
    float t{1.0F};
    Vector3 point{};
    Vector3 normal{};
    Vector3 surface{};
    int steps{0};
};

[[nodiscard]] constexpr SweepResult sweep_miss() noexcept {
    return SweepResult{};
}

// `subject` mask selects `target` layers; a pair is never a hit when the
// shapes belong to the same entity or either descriptor is missing.
[[nodiscard]] bool touching(const std::optional<ColliderShape>& subject,
                            const std::optional<ColliderShape>& target,
                            bool same_entity) noexcept;

// Where `subject` first touches `target` when moved by `delta`, by
// conservative advancement over the separating plane.
[[nodiscard]] SweepResult sweep(const std::optional<ColliderShape>& subject,
                                Vector3 delta,
                                const std::optional<ColliderShape>& target,
                                bool same_entity) noexcept;

// Shortest translation that moves `subject` out of `target`, zero when they
// don't overlap; two bodies whose masks select each other's layers split it.
[[nodiscard]] Vector3 push_out(const std::optional<ColliderShape>& subject,
                               const std::optional<ColliderShape>& target,
                               bool same_entity,
                               bool both_bodies) noexcept;

struct Bounds {
    Vector3 min{};
    Vector3 max{};
};

[[nodiscard]] Bounds bounds(const ColliderShape& shape) noexcept;
// Encloses the shape at its start and at `center + delta`, so every position in between.
[[nodiscard]] Bounds swept_bounds(const ColliderShape& shape, Vector3 delta) noexcept;

}  // namespace cactus::runtime::physics
