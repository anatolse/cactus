#pragma once

#include <entt/entt.hpp>
#include <raylib.h>

#include <cmath>
#include <limits>
#include <vector>

namespace cactus::runtime::entt_backend {

// ── Spatial query helpers (std.physics.flat.query / std.physics.volume.query) ─
// Shared search algorithms behind the cpp-entt backend's 2D/3D spatial
// queries. Each generated call site passes its own (already trait-filtered)
// view and a position_of callback that resolves the WorldTransform component
// name, which differs between the flat and volume backends and is only known
// at codegen time.

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_nearest(View view, const Vector2& from, PositionOf position_of) {
    entt::entity best{entt::null};
    float best_d = std::numeric_limits<float>::max();
    for (auto e : view) {
        const Vector2 pos = position_of(e);
        const float dx    = pos.x - from.x;
        const float dy    = pos.y - from.y;
        const float d     = (dx * dx) + (dy * dy);
        if (d < best_d) {
            best_d = d;
            best   = e;
        }
    }
    return best;
}

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_nearest(View view, const Vector3& from, PositionOf position_of) {
    entt::entity best{entt::null};
    float best_d = std::numeric_limits<float>::max();
    for (auto e : view) {
        const Vector3 pos = position_of(e);
        const float dx    = pos.x - from.x;
        const float dy    = pos.y - from.y;
        const float dz    = pos.z - from.z;
        const float d     = (dx * dx) + (dy * dy) + (dz * dz);
        if (d < best_d) {
            best_d = d;
            best   = e;
        }
    }
    return best;
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_box(View view,
                                                          const Vector2& center,
                                                          const Vector2& size,
                                                          PositionOf position_of) {
    std::vector<entt::entity> result;
    const float hx = size.x * 0.5F;
    const float hy = size.y * 0.5F;
    for (auto e : view) {
        const Vector2 pos = position_of(e);
        if (std::abs(pos.x - center.x) <= hx && std::abs(pos.y - center.y) <= hy) {
            result.push_back(e);
        }
    }
    return result;
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_box(View view,
                                                          const Vector3& center,
                                                          const Vector3& size,
                                                          PositionOf position_of) {
    std::vector<entt::entity> result;
    const float hx = size.x * 0.5F;
    const float hy = size.y * 0.5F;
    const float hz = size.z * 0.5F;
    for (auto e : view) {
        const Vector3 pos = position_of(e);
        if (std::abs(pos.x - center.x) <= hx && std::abs(pos.y - center.y) <= hy && std::abs(pos.z - center.z) <= hz) {
            result.push_back(e);
        }
    }
    return result;
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_circle(View view,
                                                             const Vector2& center,
                                                             float radius,
                                                             PositionOf position_of) {
    std::vector<entt::entity> result;
    for (auto e : view) {
        const Vector2 pos = position_of(e);
        const float dx    = pos.x - center.x;
        const float dy    = pos.y - center.y;
        if (((dx * dx) + (dy * dy)) <= (radius * radius)) {
            result.push_back(e);
        }
    }
    return result;
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_sphere(View view,
                                                             const Vector3& center,
                                                             float radius,
                                                             PositionOf position_of) {
    std::vector<entt::entity> result;
    for (auto e : view) {
        const Vector3 pos = position_of(e);
        const float dx    = pos.x - center.x;
        const float dy    = pos.y - center.y;
        const float dz    = pos.z - center.z;
        if (((dx * dx) + (dy * dy) + (dz * dz)) <= (radius * radius)) {
            result.push_back(e);
        }
    }
    return result;
}

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_raycast(View view,
                                         const Vector2& origin,
                                         const Vector2& dir,
                                         float max_dist,
                                         PositionOf position_of) {
    entt::entity best{entt::null};
    float best_d = std::numeric_limits<float>::max();
    for (auto e : view) {
        const Vector2 pos = position_of(e);
        const float dx    = pos.x - origin.x;
        const float dy    = pos.y - origin.y;
        const float proj  = (dx * dir.x) + (dy * dir.y);
        if (proj >= 0.0F && proj <= max_dist) {
            const float perp = (dx * dir.y) - (dy * dir.x);
            if (std::abs(perp) < 0.5F && proj < best_d) {
                best_d = proj;
                best   = e;
            }
        }
    }
    return best;
}

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_raycast(View view,
                                         const Vector3& origin,
                                         const Vector3& dir,
                                         float max_dist,
                                         PositionOf position_of) {
    entt::entity best{entt::null};
    float best_d = std::numeric_limits<float>::max();
    for (auto e : view) {
        const Vector3 pos = position_of(e);
        const float dx    = pos.x - origin.x;
        const float dy    = pos.y - origin.y;
        const float dz    = pos.z - origin.z;
        const float proj  = (dx * dir.x) + (dy * dir.y) + (dz * dir.z);
        if (proj >= 0.0F && proj <= max_dist) {
            const float perp_x = (dy * dir.z) - (dz * dir.y);
            const float perp_y = (dz * dir.x) - (dx * dir.z);
            const float perp_z = (dx * dir.y) - (dy * dir.x);
            if (((perp_x * perp_x) + (perp_y * perp_y) + (perp_z * perp_z)) < 0.25F && proj < best_d) {
                best_d = proj;
                best   = e;
            }
        }
    }
    return best;
}

}  // namespace cactus::runtime::entt_backend
