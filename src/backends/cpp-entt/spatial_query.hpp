#pragma once

#include "backends/cpp-entt/creation_order.hpp"

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
// at codegen time. Exact distance ties go to the oldest entity, and lists
// come back oldest first, so no result follows storage order.

// The closest candidate offered so far.
class Closest {
public:
    explicit Closest(const entt::registry& registry) : registry_(&registry) {}

    void offer(entt::entity candidate, float distance) {
        const bool closer =
            best_ == entt::null || distance < best_distance_ ||
            (distance == best_distance_ && creation_ordinal(*registry_, candidate) < creation_ordinal(*registry_, best_));
        if (closer) {
            best_          = candidate;
            best_distance_ = distance;
        }
    }

    [[nodiscard]] entt::entity entity() const {
        return best_;
    }

private:
    const entt::registry* registry_;
    entt::entity best_{entt::null};
    float best_distance_ = std::numeric_limits<float>::max();
};

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_nearest(const entt::registry& registry,
                                         View view,
                                         const Vector2& from,
                                         PositionOf position_of) {
    Closest closest(registry);
    for (auto e : view) {
        const Vector2 pos = position_of(e);
        const float dx    = pos.x - from.x;
        const float dy    = pos.y - from.y;
        const float d     = (dx * dx) + (dy * dy);
        closest.offer(e, d);
    }
    return closest.entity();
}

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_nearest(const entt::registry& registry,
                                         View view,
                                         const Vector3& from,
                                         PositionOf position_of) {
    Closest closest(registry);
    for (auto e : view) {
        const Vector3 pos = position_of(e);
        const float dx    = pos.x - from.x;
        const float dy    = pos.y - from.y;
        const float dz    = pos.z - from.z;
        const float d     = (dx * dx) + (dy * dy) + (dz * dz);
        closest.offer(e, d);
    }
    return closest.entity();
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_box(const entt::registry& registry,
                                                          View view,
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
    return creation_ordered(registry, result);
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_box(const entt::registry& registry,
                                                          View view,
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
    return creation_ordered(registry, result);
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_circle(const entt::registry& registry,
                                                             View view,
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
    return creation_ordered(registry, result);
}

template <typename View, typename PositionOf>
[[nodiscard]] std::vector<entt::entity> query_overlap_sphere(const entt::registry& registry,
                                                             View view,
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
    return creation_ordered(registry, result);
}

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_raycast(const entt::registry& registry,
                                         View view,
                                         const Vector2& origin,
                                         const Vector2& dir,
                                         float max_dist,
                                         PositionOf position_of) {
    Closest closest(registry);
    for (auto e : view) {
        const Vector2 pos = position_of(e);
        const float dx    = pos.x - origin.x;
        const float dy    = pos.y - origin.y;
        const float proj  = (dx * dir.x) + (dy * dir.y);
        if (proj >= 0.0F && proj <= max_dist) {
            const float perp = (dx * dir.y) - (dy * dir.x);
            if (std::abs(perp) < 0.5F) {
                closest.offer(e, proj);
            }
        }
    }
    return closest.entity();
}

template <typename View, typename PositionOf>
[[nodiscard]] entt::entity query_raycast(const entt::registry& registry,
                                         View view,
                                         const Vector3& origin,
                                         const Vector3& dir,
                                         float max_dist,
                                         PositionOf position_of) {
    Closest closest(registry);
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
            if (((perp_x * perp_x) + (perp_y * perp_y) + (perp_z * perp_z)) < 0.25F) {
                closest.offer(e, proj);
            }
        }
    }
    return closest.entity();
}

}  // namespace cactus::runtime::entt_backend
