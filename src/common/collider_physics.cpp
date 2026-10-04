#include "common/collider_physics.hpp"

#include "common/cactus_runtime.hpp"

#include <raymath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace cactus::runtime::physics {

namespace {

constexpr Vector3 kUp{.x = 0.0F, .y = 1.0F, .z = 0.0F};
constexpr int kMaxAdvanceSteps  = 32;
constexpr int kMaxGjkIterations = 32;
// Below this core distance the GJK direction is too short to trust as a normal.
constexpr float kCoreContact = 1e-6F;
constexpr float kTinySquared = 1e-12F;
constexpr float kGjkRelativeTolerance = 1e-5F;
// Edge-edge SAT axes must beat the best face axis by this much, so face contacts stay stable.
constexpr float kSatFaceBias = 1e-4F;

float dot(Vector3 a, Vector3 b) noexcept {
    return Vector3DotProduct(a, b);
}

Vector3 scaled(Vector3 v, float s) noexcept {
    return Vector3Scale(v, s);
}

Vector3 rotate(Quaternion q, Vector3 v) noexcept {
    return Vector3RotateByQuaternion(v, q);
}

Vector3 unrotate(Quaternion q, Vector3 v) noexcept {
    return Vector3RotateByQuaternion(v, Quaternion{.x = -q.x, .y = -q.y, .z = -q.z, .w = q.w});
}

float& axis_ref(Vector3& v, int axis) noexcept {
    switch (axis) {
        case 0:
            return v.x;
        case 1:
            return v.y;
        default:
            return v.z;
    }
}

float component(Vector3 v, int axis) noexcept {
    return axis_ref(v, axis);
}

void set_component(Vector3& v, int axis, float value) noexcept {
    axis_ref(v, axis) = value;
}

Vector3 unit_axis(int axis) noexcept {
    Vector3 v{};
    set_component(v, axis, 1.0F);
    return v;
}

std::array<Vector3, 3> box_axes(const ColliderShape& box) noexcept {
    return {rotate(box.rotation, unit_axis(0)), rotate(box.rotation, unit_axis(1)), rotate(box.rotation, unit_axis(2))};
}

Vector3 core_support(const ColliderShape& shape, Vector3 direction) noexcept {
    switch (shape.kind) {
        case ShapeKind::Sphere:
            return shape.center;
        case ShapeKind::Capsule:
            return shape.center + Vector3{.x = 0.0F, .y = direction.y >= 0.0F ? shape.half_segment : -shape.half_segment, .z = 0.0F};
        case ShapeKind::Box: {
            const Vector3 local = unrotate(shape.rotation, direction);
            const Vector3 corner{.x = local.x >= 0.0F ? shape.half_extents.x : -shape.half_extents.x,
                                 .y = local.y >= 0.0F ? shape.half_extents.y : -shape.half_extents.y,
                                 .z = local.z >= 0.0F ? shape.half_extents.z : -shape.half_extents.z};
            return shape.center + rotate(shape.rotation, corner);
        }
    }
    std::unreachable();
}

// The core point nearest `target`: a sphere's center, or the clamped point on
// a capsule's segment. A box answers with its center.
Vector3 nearest_core_point(const ColliderShape& shape, Vector3 target) noexcept {
    if (shape.kind != ShapeKind::Capsule) {
        return shape.center;
    }
    const float y = std::clamp(target.y, shape.center.y - shape.half_segment, shape.center.y + shape.half_segment);
    return Vector3{.x = shape.center.x, .y = y, .z = shape.center.z};
}

// ── GJK ─────────────────────────────────────────────────────────────────────

struct Vertex {
    Vector3 w;  // a - b
    Vector3 a;
    Vector3 b;
};

struct Simplex {
    std::array<Vertex, 4> vertices{};
    std::array<float, 4> weights{};
    int size = 0;

    void keep(std::initializer_list<std::pair<int, float>> kept) noexcept {
        std::array<Vertex, 4> next{};
        int count = 0;
        for (const auto& [index, weight] : kept) {
            next[static_cast<std::size_t>(count)]    = vertices[static_cast<std::size_t>(index)];
            weights[static_cast<std::size_t>(count)] = weight;
            ++count;
        }
        vertices = next;
        size     = count;
    }

    [[nodiscard]] Vector3 point() const noexcept {
        Vector3 sum{};
        for (int i = 0; i < size; ++i) {
            sum = sum + scaled(vertices[static_cast<std::size_t>(i)].w, weights[static_cast<std::size_t>(i)]);
        }
        return sum;
    }
};

Vertex minkowski_support(const ColliderShape& a, const ColliderShape& b, Vector3 direction) noexcept {
    const Vector3 on_a = core_support(a, direction);
    const Vector3 on_b = core_support(b, Vector3Negate(direction));
    return Vertex{.w = on_a - on_b, .a = on_a, .b = on_b};
}

// Each reducer below keeps only the simplex feature nearest the origin, with
// barycentric weights, following Ericson's closest-point routines.
void reduce_segment(Simplex& simplex) noexcept {
    const Vector3 a  = simplex.vertices[0].w;
    const Vector3 ab = simplex.vertices[1].w - a;
    const float denom = dot(ab, ab);
    if (denom <= kTinySquared) {
        simplex.keep({{0, 1.0F}});
        return;
    }
    const float t = std::clamp(-dot(a, ab) / denom, 0.0F, 1.0F);
    if (t <= 0.0F) {
        simplex.keep({{0, 1.0F}});
    } else if (t >= 1.0F) {
        simplex.keep({{1, 1.0F}});
    } else {
        simplex.keep({{0, 1.0F - t}, {1, t}});
    }
}

// Picks whichever edge of a degenerate (collinear) triangle lies nearest the origin.
void reduce_degenerate_triangle(Simplex& simplex) noexcept {
    Simplex best;
    float best_distance = std::numeric_limits<float>::max();
    for (const auto& [i, j] : std::array<std::pair<int, int>, 3>{{{0, 1}, {1, 2}, {0, 2}}}) {
        Simplex edge;
        edge.vertices[0] = simplex.vertices[static_cast<std::size_t>(i)];
        edge.vertices[1] = simplex.vertices[static_cast<std::size_t>(j)];
        edge.size        = 2;
        reduce_segment(edge);
        const float distance = Vector3LengthSqr(edge.point());
        if (distance < best_distance) {
            best_distance = distance;
            best          = edge;
        }
    }
    simplex = best;
}

void reduce_triangle(Simplex& simplex) noexcept {
    const Vector3 a  = simplex.vertices[0].w;
    const Vector3 b  = simplex.vertices[1].w;
    const Vector3 c  = simplex.vertices[2].w;
    const Vector3 ab = b - a;
    const Vector3 ac = c - a;
    const Vector3 ap = Vector3Negate(a);
    const float d1   = dot(ab, ap);
    const float d2   = dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        simplex.keep({{0, 1.0F}});
        return;
    }
    const Vector3 bp = Vector3Negate(b);
    const float d3   = dot(ab, bp);
    const float d4   = dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        simplex.keep({{1, 1.0F}});
        return;
    }
    const float vc = (d1 * d4) - (d3 * d2);
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        const float v = d1 / (d1 - d3);
        simplex.keep({{0, 1.0F - v}, {1, v}});
        return;
    }
    const Vector3 cp = Vector3Negate(c);
    const float d5   = dot(ab, cp);
    const float d6   = dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        simplex.keep({{2, 1.0F}});
        return;
    }
    const float vb = (d5 * d2) - (d1 * d6);
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        const float w = d2 / (d2 - d6);
        simplex.keep({{0, 1.0F - w}, {2, w}});
        return;
    }
    const float va = (d3 * d6) - (d5 * d4);
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        simplex.keep({{1, 1.0F - w}, {2, w}});
        return;
    }
    const float sum = va + vb + vc;
    if (sum <= kTinySquared) {
        reduce_degenerate_triangle(simplex);
        return;
    }
    const float v = vb / sum;
    const float w = vc / sum;
    simplex.keep({{0, 1.0F - v - w}, {1, v}, {2, w}});
}

// Returns false when the origin lies inside the tetrahedron.
bool reduce_tetrahedron(Simplex& simplex) noexcept {
    static constexpr std::array<std::array<int, 4>, 4> kFaces{{{0, 1, 2, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {1, 3, 2, 0}}};
    Simplex best;
    float best_distance = std::numeric_limits<float>::max();
    bool outside_any    = false;
    for (const auto& face : kFaces) {
        const Vector3 a      = simplex.vertices[static_cast<std::size_t>(face[0])].w;
        const Vector3 normal = Vector3CrossProduct(simplex.vertices[static_cast<std::size_t>(face[1])].w - a,
                                                   simplex.vertices[static_cast<std::size_t>(face[2])].w - a);
        const float origin_side   = dot(Vector3Negate(a), normal);
        const float opposite_side = dot(simplex.vertices[static_cast<std::size_t>(face[3])].w - a, normal);
        const bool degenerate     = std::abs(opposite_side) <= kTinySquared;
        if (!degenerate && origin_side * opposite_side >= 0.0F) {
            continue;
        }
        outside_any = true;
        Simplex triangle;
        for (std::size_t i = 0; i < 3; ++i) {
            triangle.vertices[i] = simplex.vertices[static_cast<std::size_t>(face[i])];
        }
        triangle.size = 3;
        reduce_triangle(triangle);
        const float distance = Vector3LengthSqr(triangle.point());
        if (distance < best_distance) {
            best_distance = distance;
            best          = triangle;
        }
    }
    if (!outside_any) {
        return false;
    }
    simplex = best;
    return true;
}

// Returns false when the simplex encloses the origin.
bool reduce(Simplex& simplex) noexcept {
    switch (simplex.size) {
        case 1:
            simplex.weights[0] = 1.0F;
            return true;
        case 2:
            reduce_segment(simplex);
            return true;
        case 3:
            reduce_triangle(simplex);
            return true;
        default:
            return reduce_tetrahedron(simplex);
    }
}

bool contains_vertex(const Simplex& simplex, Vector3 w) noexcept {
    for (int i = 0; i < simplex.size; ++i) {
        if (Vector3LengthSqr(simplex.vertices[static_cast<std::size_t>(i)].w - w) <= kTinySquared) {
            return true;
        }
    }
    return false;
}

// ── Penetration ─────────────────────────────────────────────────────────────

struct Push {
    Vector3 normal;
    float depth;
};

// Pushes `point` out of `box` through the face it is least deep behind.
Push box_push(const ColliderShape& box, Vector3 point) noexcept {
    const Vector3 local = unrotate(box.rotation, point - box.center);
    int axis            = 0;
    float depth         = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        const float axis_depth = component(box.half_extents, i) - std::abs(component(local, i));
        if (axis_depth < depth) {
            depth = axis_depth;
            axis  = i;
        }
    }
    const float sign = component(local, axis) >= 0.0F ? 1.0F : -1.0F;
    return Push{.normal = rotate(box.rotation, scaled(unit_axis(axis), sign)), .depth = std::max(depth, 0.0F)};
}

float projected_radius(const ColliderShape& box, const std::array<Vector3, 3>& axes, Vector3 direction) noexcept {
    return (box.half_extents.x * std::abs(dot(axes[0], direction))) +
           (box.half_extents.y * std::abs(dot(axes[1], direction))) +
           (box.half_extents.z * std::abs(dot(axes[2], direction)));
}

// Least-overlap axis among the 15 box-box separating-axis candidates.
Push box_box_push(const ColliderShape& a, const ColliderShape& b) noexcept {
    const auto axes_a     = box_axes(a);
    const auto axes_b     = box_axes(b);
    const Vector3 between = a.center - b.center;
    Push best{.normal = kUp, .depth = std::numeric_limits<float>::max()};
    const auto consider = [&](Vector3 axis, float bias) {
        const float distance = dot(between, axis);
        const float overlap  = projected_radius(a, axes_a, axis) + projected_radius(b, axes_b, axis) - std::abs(distance);
        if (overlap + bias < best.depth) {
            best = Push{.normal = distance >= 0.0F ? axis : Vector3Negate(axis), .depth = std::max(overlap, 0.0F)};
        }
    };
    for (const auto& axis : axes_a) {
        consider(axis, 0.0F);
    }
    for (const auto& axis : axes_b) {
        consider(axis, 0.0F);
    }
    for (const auto& edge_a : axes_a) {
        for (const auto& edge_b : axes_b) {
            const Vector3 cross = Vector3CrossProduct(edge_a, edge_b);
            const float length  = Vector3Length(cross);
            if (length > kCoreContact) {
                consider(scaled(cross, 1.0F / length), kSatFaceBias);
            }
        }
    }
    return best;
}

Push round_push(const ColliderShape& a, const ColliderShape& b) noexcept {
    const Vector3 between = a.center - b.center;
    const float length    = Vector3Length(between);
    return Push{.normal = length > kCoreContact ? scaled(between, 1.0F / length) : kUp, .depth = 0.0F};
}

// Normal from `b` toward `a` for overlapping cores.
Push penetration_push(const ColliderShape& a, const ColliderShape& b) noexcept {
    const bool a_box = a.kind == ShapeKind::Box;
    const bool b_box = b.kind == ShapeKind::Box;
    if (a_box && b_box) {
        return box_box_push(a, b);
    }
    if (b_box) {
        return box_push(b, nearest_core_point(a, b.center));
    }
    if (a_box) {
        const Push push = box_push(a, nearest_core_point(b, a.center));
        return Push{.normal = Vector3Negate(push.normal), .depth = push.depth};
    }
    return round_push(a, b);
}

// A point on `shape`'s surface facing along `normal`, near `reference`.
Vector3 surface_point(const ColliderShape& shape, Vector3 reference, Vector3 normal) noexcept {
    if (shape.kind != ShapeKind::Box) {
        return nearest_core_point(shape, reference) + scaled(normal, shape.radius);
    }
    Vector3 local = unrotate(shape.rotation, reference - shape.center);
    local         = Vector3{.x = std::clamp(local.x, -shape.half_extents.x, shape.half_extents.x),
                            .y = std::clamp(local.y, -shape.half_extents.y, shape.half_extents.y),
                            .z = std::clamp(local.z, -shape.half_extents.z, shape.half_extents.z)};
    const Vector3 local_normal = unrotate(shape.rotation, normal);
    int axis                   = 0;
    for (int i = 1; i < 3; ++i) {
        if (std::abs(component(local_normal, i)) > std::abs(component(local_normal, axis))) {
            axis = i;
        }
    }
    const float sign = component(local_normal, axis) >= 0.0F ? 1.0F : -1.0F;
    set_component(local, axis, sign * component(shape.half_extents, axis));
    return shape.center + rotate(shape.rotation, local);
}

bool layers_collide(const ColliderShape& subject, const ColliderShape& target) noexcept {
    return (subject.mask & target.layer) != 0;
}

SweepResult sweep_shapes(const ColliderShape& subject, Vector3 delta, const ColliderShape& target) noexcept {
    ColliderShape moving = subject;
    float t              = 0.0F;
    Separation gap;
    for (int step = 1; step <= kMaxAdvanceSteps; ++step) {
        moving.center = subject.center + scaled(delta, t);
        gap           = separation(moving, target);
        if (gap.distance <= kSkin) {
            return SweepResult{.hit = true, .t = t, .point = gap.point, .normal = gap.normal, .steps = step};
        }
        const float closing = -dot(delta, gap.normal);
        if (closing <= 0.0F) {
            return SweepResult{.steps = step};
        }
        t += gap.distance / closing;
        if (t > 1.0F) {
            return SweepResult{.steps = step};
        }
    }
    return SweepResult{.hit = true, .t = t, .point = gap.point, .normal = gap.normal, .steps = kMaxAdvanceSteps};
}

Bounds bounds_around(Vector3 center, Vector3 extent) noexcept {
    return Bounds{.min = center - extent, .max = center + extent};
}

}  // namespace

ColliderShape box_shape(Vector3 center, Quaternion rotation, Vector3 size, int layer, int mask) noexcept {
    return ColliderShape{.kind         = ShapeKind::Box,
                         .center       = center,
                         .rotation     = stdlib::math::quat::normalize(rotation),
                         .half_extents = Vector3{.x = std::abs(size.x) / 2.0F, .y = std::abs(size.y) / 2.0F, .z = std::abs(size.z) / 2.0F},
                         .layer        = layer,
                         .mask         = mask};
}

ColliderShape sphere_shape(Vector3 center, float radius, int layer, int mask) noexcept {
    return ColliderShape{.kind = ShapeKind::Sphere, .center = center, .radius = std::abs(radius), .layer = layer, .mask = mask};
}

ColliderShape capsule_shape(Vector3 center, float radius, float height, int layer, int mask) noexcept {
    const float r = std::abs(radius);
    return ColliderShape{.kind         = ShapeKind::Capsule,
                         .center       = center,
                         .radius       = r,
                         .half_segment = std::max(0.0F, (std::abs(height) / 2.0F) - r),
                         .layer        = layer,
                         .mask         = mask};
}

CoreDistance core_distance(const ColliderShape& a, const ColliderShape& b) noexcept {
    Vector3 direction = a.center - b.center;
    if (Vector3LengthSqr(direction) <= kTinySquared) {
        direction = unit_axis(0);
    }
    Simplex simplex;
    simplex.vertices[0] = minkowski_support(a, b, direction);
    simplex.weights[0]  = 1.0F;
    simplex.size        = 1;
    Vector3 closest     = simplex.vertices[0].w;
    bool overlap        = false;
    for (int iteration = 0; iteration < kMaxGjkIterations; ++iteration) {
        const float distance_sq = Vector3LengthSqr(closest);
        if (distance_sq <= kTinySquared) {
            overlap = true;
            break;
        }
        const Vertex next = minkowski_support(a, b, Vector3Negate(closest));
        if (distance_sq - dot(closest, next.w) <= kGjkRelativeTolerance * distance_sq || contains_vertex(simplex, next.w)) {
            break;
        }
        simplex.vertices[static_cast<std::size_t>(simplex.size)] = next;
        ++simplex.size;
        if (!reduce(simplex)) {
            overlap = true;
            break;
        }
        closest = simplex.point();
    }
    CoreDistance result{.overlap = overlap};
    for (int i = 0; i < simplex.size; ++i) {
        const auto index = static_cast<std::size_t>(i);
        result.on_a      = result.on_a + scaled(simplex.vertices[index].a, simplex.weights[index]);
        result.on_b      = result.on_b + scaled(simplex.vertices[index].b, simplex.weights[index]);
    }
    result.distance = overlap ? 0.0F : Vector3Length(closest);
    return result;
}

Separation separation(const ColliderShape& a, const ColliderShape& b) noexcept {
    const CoreDistance cores = core_distance(a, b);
    const float radii        = a.radius + b.radius;
    if (!cores.overlap && cores.distance > kCoreContact) {
        const Vector3 normal = scaled(cores.on_a - cores.on_b, 1.0F / cores.distance);
        return Separation{
            .distance = cores.distance - radii, .normal = normal, .point = cores.on_b + scaled(normal, b.radius)};
    }
    const Push push = penetration_push(a, b);
    const Vector3 reference = a.kind == ShapeKind::Box ? a.center : nearest_core_point(a, b.center);
    return Separation{.distance = -(radii + push.depth),
                      .normal   = push.normal,
                      .point    = surface_point(b, reference, push.normal)};
}

bool touching(const std::optional<ColliderShape>& subject,
              const std::optional<ColliderShape>& target,
              bool same_entity) noexcept {
    if (same_entity || !subject.has_value() || !target.has_value() || !layers_collide(*subject, *target)) {
        return false;
    }
    const CoreDistance cores = core_distance(*subject, *target);
    return cores.overlap || cores.distance <= subject->radius + target->radius;
}

SweepResult sweep(const std::optional<ColliderShape>& subject,
                  Vector3 delta,
                  const std::optional<ColliderShape>& target,
                  bool same_entity) noexcept {
    if (same_entity || !subject.has_value() || !target.has_value() || !layers_collide(*subject, *target)) {
        return sweep_miss();
    }
    return sweep_shapes(*subject, delta, *target);
}

Bounds bounds(const ColliderShape& shape) noexcept {
    switch (shape.kind) {
        case ShapeKind::Sphere:
            return bounds_around(shape.center, Vector3{.x = shape.radius, .y = shape.radius, .z = shape.radius});
        case ShapeKind::Capsule:
            return bounds_around(shape.center,
                                 Vector3{.x = shape.radius, .y = shape.half_segment + shape.radius, .z = shape.radius});
        case ShapeKind::Box: {
            const auto axes = box_axes(shape);
            return bounds_around(shape.center,
                                 Vector3{.x = projected_radius(shape, axes, unit_axis(0)),
                                         .y = projected_radius(shape, axes, unit_axis(1)),
                                         .z = projected_radius(shape, axes, unit_axis(2))});
        }
    }
    std::unreachable();
}

Bounds swept_bounds(const ColliderShape& shape, Vector3 delta) noexcept {
    const Bounds start = bounds(shape);
    return Bounds{.min = Vector3Min(start.min, start.min + delta), .max = Vector3Max(start.max, start.max + delta)};
}

}  // namespace cactus::runtime::physics
