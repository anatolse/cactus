#include "common/cactus_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <sstream>

namespace cactus::runtime {

namespace {
[[maybe_unused]] constexpr auto kCommonRuntimeName = common_runtime_name();

[[nodiscard]] constexpr float length_squared(Vector2 v) noexcept {
    return (v.x * v.x) + (v.y * v.y);
}

[[nodiscard]] constexpr float length_squared(Vector3 v) noexcept {
    return (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
}

std::string asset_kind_name(const AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture:
            return "texture";
        case AssetKind::Mesh:
            return "mesh";
        case AssetKind::Material:
            return "material";
    }
    return "asset";
}
}  // namespace

void AssetRegistry::clear() {
    textures_.clear();
    meshes_.clear();
    materials_.clear();
    texture_resolver_  = {};
    mesh_resolver_     = {};
    material_resolver_ = {};
    clear_diagnostics();
}

void AssetRegistry::clear_diagnostics() {
    diagnostics_.clear();
    missing_count_ = 0;
}

void AssetRegistry::register_asset(const AssetKind kind,
                                   const AssetHandle handle,
                                   std::string asset_id,
                                   const int runtime_id,
                                   const bool materialized) {
    map_for(kind)[handle] = AssetRecord{
        .handle       = handle,
        .kind         = kind,
        .asset_id     = std::move(asset_id),
        .runtime_id   = runtime_id,
        .materialized = materialized,
    };
}

void AssetRegistry::register_texture(const AssetHandle handle,
                                     std::string asset_id,
                                     const int runtime_id,
                                     const bool materialized) {
    register_asset(AssetKind::Texture, handle, std::move(asset_id), runtime_id, materialized);
}

void AssetRegistry::register_mesh(const AssetHandle handle,
                                  std::string asset_id,
                                  const int runtime_id,
                                  const bool materialized) {
    register_asset(AssetKind::Mesh, handle, std::move(asset_id), runtime_id, materialized);
}

void AssetRegistry::register_material(const AssetHandle handle,
                                      std::string asset_id,
                                      const int runtime_id,
                                      const bool materialized) {
    register_asset(AssetKind::Material, handle, std::move(asset_id), runtime_id, materialized);
}

void AssetRegistry::set_lazy_resolver(const AssetKind kind, LazyResolver resolver) {
    resolver_for(kind) = std::move(resolver);
}

AssetResolution AssetRegistry::resolve(const AssetKind kind, const AssetHandle handle) {
    auto& assets = map_for(kind);
    if (const auto it = assets.find(handle); it != assets.end()) {
        return AssetResolution{
            .handle     = handle,
            .kind       = kind,
            .status     = it->second.materialized ? AssetStatus::Materialized : AssetStatus::Registered,
            .runtime_id = it->second.runtime_id,
        };
    }

    auto& resolver = resolver_for(kind);
    if (resolver) {
        if (auto loaded = resolver(handle); loaded.has_value()) {
            AssetRecord record = std::move(*loaded);
            record.handle      = handle;
            record.kind        = kind;
            assets.emplace(handle, record);
            return AssetResolution{
                .handle     = handle,
                .kind       = kind,
                .status     = record.materialized ? AssetStatus::Materialized : AssetStatus::Registered,
                .runtime_id = record.runtime_id,
            };
        }
    }

    ++missing_count_;
    std::ostringstream message;
    message << "missing " << asset_kind_name(kind) << " asset handle " << handle;
    diagnostics_.push_back(message.str());
    return AssetResolution{.handle = handle, .kind = kind, .status = AssetStatus::Missing, .runtime_id = -1};
}

AssetRegistry::AssetMap& AssetRegistry::map_for(const AssetKind kind) noexcept {
    switch (kind) {
        case AssetKind::Texture:
            return textures_;
        case AssetKind::Mesh:
            return meshes_;
        case AssetKind::Material:
            return materials_;
    }
    return textures_;
}

const AssetRegistry::AssetMap& AssetRegistry::map_for(const AssetKind kind) const noexcept {
    switch (kind) {
        case AssetKind::Texture:
            return textures_;
        case AssetKind::Mesh:
            return meshes_;
        case AssetKind::Material:
            return materials_;
    }
    return textures_;
}

AssetRegistry::LazyResolver& AssetRegistry::resolver_for(const AssetKind kind) noexcept {
    switch (kind) {
        case AssetKind::Texture:
            return texture_resolver_;
        case AssetKind::Mesh:
            return mesh_resolver_;
        case AssetKind::Material:
            return material_resolver_;
    }
    return texture_resolver_;
}

AssetRegistry& shared_asset_registry() noexcept {
    static AssetRegistry registry;
    return registry;
}

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
        return Vector2{.x = 0.0F, .y = 0.0F};
    }
    const float inv_len = 1.0F / std::sqrt(len_sq);
    return Vector2{.x = v.x * inv_len, .y = v.y * inv_len};
}

float distance(Vector2 a, Vector2 b) noexcept {
    return length(Vector2{.x = a.x - b.x, .y = a.y - b.y});
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
        return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    }
    const float inv_len = 1.0F / std::sqrt(len_sq);
    return Vector3{.x = v.x * inv_len, .y = v.y * inv_len, .z = v.z * inv_len};
}

float distance(Vector3 a, Vector3 b) noexcept {
    return length(Vector3{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z});
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
    return Vector3RotateByQuaternion(Vector3{.x = 0.0F, .y = 0.0F, .z = -1.0F}, q);
}

Vector3 right(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, q);
}

Vector3 up(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, q);
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