#pragma once

#include <raylib.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cactus::runtime {

using Quat        = Quaternion;
using AssetHandle = std::uint32_t;

enum class AssetKind : std::uint8_t {
    Texture,
    Mesh,
    Material,
};

enum class AssetStatus : std::uint8_t {
    Missing,
    Registered,
    Materialized,
};

struct AssetRecord {
    AssetHandle handle{};
    AssetKind kind{AssetKind::Texture};
    std::string asset_id;
    int runtime_id{-1};
    bool materialized{false};
};

struct AssetResolution {
    AssetHandle handle{};
    AssetKind kind{AssetKind::Texture};
    AssetStatus status{AssetStatus::Missing};
    int runtime_id{-1};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return status != AssetStatus::Missing;
    }

    [[nodiscard]] constexpr bool ready() const noexcept {
        return status == AssetStatus::Materialized;
    }
};

class AssetRegistry {
public:
    using LazyResolver = std::function<std::optional<AssetRecord>(AssetHandle)>;

    void clear();
    void clear_diagnostics();

    void register_asset(AssetKind kind,
                        AssetHandle handle,
                        std::string asset_id,
                        int runtime_id,
                        bool materialized = true);
    void register_texture(AssetHandle handle, std::string asset_id, int runtime_id, bool materialized = true);
    void register_mesh(AssetHandle handle, std::string asset_id, int runtime_id, bool materialized = true);
    void register_material(AssetHandle handle, std::string asset_id, int runtime_id, bool materialized = true);

    void set_lazy_resolver(AssetKind kind, LazyResolver resolver);
    [[nodiscard]] AssetResolution resolve(AssetKind kind, AssetHandle handle);

    [[nodiscard]] std::size_t missing_count() const noexcept {
        return missing_count_;
    }
    [[nodiscard]] const std::vector<std::string>& diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    using AssetMap = std::unordered_map<AssetHandle, AssetRecord>;

    [[nodiscard]] AssetMap& map_for(AssetKind kind) noexcept;
    [[nodiscard]] const AssetMap& map_for(AssetKind kind) const noexcept;
    [[nodiscard]] LazyResolver& resolver_for(AssetKind kind) noexcept;

    AssetMap textures_;
    AssetMap meshes_;
    AssetMap materials_;
    LazyResolver texture_resolver_;
    LazyResolver mesh_resolver_;
    LazyResolver material_resolver_;
    std::vector<std::string> diagnostics_;
    std::size_t missing_count_{0};
};

[[nodiscard]] AssetRegistry& shared_asset_registry() noexcept;

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

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
    return a + ((b - a) * t);
}
[[nodiscard]] constexpr float clamp(float x, float lo, float hi) noexcept {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}
[[nodiscard]] constexpr float abs(float v) noexcept {
    return (v < 0.0F) ? -v : v;
}
[[nodiscard]] constexpr float min(float a, float b) noexcept {
    return (a < b) ? a : b;
}
[[nodiscard]] constexpr float max(float a, float b) noexcept {
    return (a > b) ? a : b;
}
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
[[nodiscard]] constexpr float dot(Vector2 a, Vector2 b) noexcept {
    return (a.x * b.x) + (a.y * b.y);
}
[[nodiscard]] constexpr Vector2 lerp(Vector2 a, Vector2 b, float t) noexcept {
    return Vector2{.x = stdlib::math::lerp(a.x, b.x, t), .y = stdlib::math::lerp(a.y, b.y, t)};
}
[[nodiscard]] float distance(Vector2 a, Vector2 b) noexcept;
[[nodiscard]] float angle(Vector2 v) noexcept;
}  // namespace vec2

namespace vec3 {
[[nodiscard]] float length(Vector3 v) noexcept;
[[nodiscard]] Vector3 normalize(Vector3 v) noexcept;
[[nodiscard]] constexpr float dot(Vector3 a, Vector3 b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
[[nodiscard]] constexpr Vector3 cross(Vector3 a, Vector3 b) noexcept {
    return Vector3{
        .x = (a.y * b.z) - (a.z * b.y),
        .y = (a.z * b.x) - (a.x * b.z),
        .z = (a.x * b.y) - (a.y * b.x),
    };
}
[[nodiscard]] constexpr Vector3 lerp(Vector3 a, Vector3 b, float t) noexcept {
    return Vector3{
        .x = stdlib::math::lerp(a.x, b.x, t),
        .y = stdlib::math::lerp(a.y, b.y, t),
        .z = stdlib::math::lerp(a.z, b.z, t),
    };
}
[[nodiscard]] float distance(Vector3 a, Vector3 b) noexcept;
[[nodiscard]] constexpr Vector3 reflect(Vector3 v, Vector3 normal) noexcept {
    const float scale = 2.0F * dot(v, normal);
    return Vector3{
        .x = v.x - (scale * normal.x),
        .y = v.y - (scale * normal.y),
        .z = v.z - (scale * normal.z),
    };
}
}  // namespace vec3

namespace quat {
[[nodiscard]] constexpr Quat identity() noexcept {
    return Quat{0.0F, 0.0F, 0.0F, 1.0F};
}
[[nodiscard]] Quat from_euler(float pitch, float yaw, float roll) noexcept;
[[nodiscard]] Quat from_axis_angle(Vector3 axis, float angle) noexcept;
[[nodiscard]] Vector3 forward(Quat q) noexcept;
[[nodiscard]] Vector3 right(Quat q) noexcept;
[[nodiscard]] Vector3 up(Quat q) noexcept;
[[nodiscard]] Vector3 rotate(Quat q, Vector3 v) noexcept;
[[nodiscard]] Quat slerp(Quat a, Quat b, float t) noexcept;
[[nodiscard]] constexpr Quat multiply(Quat a, Quat b) noexcept {
    return Quat{
        .x = (a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y),
        .y = (a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
        .z = (a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w),
        .w = (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z),
    };
}
[[nodiscard]] Quat inverse(Quat q) noexcept;
}  // namespace quat

}  // namespace stdlib::math

}  // namespace cactus::runtime