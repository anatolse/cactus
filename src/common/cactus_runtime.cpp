#include "common/float_mode.hpp"

#include "common/cactus_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <random>
#include <raymath.h>
#include <sstream>

// Correctly rounded binary32 functions from CORE-MATH: one result on every platform.
extern "C" {
float cr_sinf(float x);
float cr_cosf(float x);
float cr_atan2f(float y, float x);
float cr_powf(float x, float y);
}

// Global scope, matching the header declaration (cactus_runtime.hpp):
// generated code calls this unqualified, the same ordinary-lookup reason
// vec2/vec3's constructors and the Color operator matrix live at global
// scope rather than in cactus::runtime.
Color color_from_components(const float r, const float g, const float b, const float a) noexcept {
    auto channel = [](const float v) {
        return static_cast<unsigned char>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
    };
    return Color{.r = channel(r), .g = channel(g), .b = channel(b), .a = channel(a)};
}

namespace cactus::runtime {

namespace {
[[maybe_unused]] constexpr auto kCommonRuntimeName = common_runtime_name();

[[nodiscard]] constexpr float length_squared(Vector2 v) noexcept {
    return (v.x * v.x) + (v.y * v.y);
}

[[nodiscard]] constexpr float length_squared(Vector3 v) noexcept {
    return (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
}

[[nodiscard]] constexpr float length_squared(Quat q) noexcept {
    return (q.x * q.x) + (q.y * q.y) + (q.z * q.z) + (q.w * q.w);
}

[[nodiscard]] Quat normalize_or_identity(Quat q) noexcept {
    const float len_sq = length_squared(q);
    if (len_sq <= 0.0F) {
        return stdlib::math::quat::identity();
    }
    const float inv_len = 1.0F / std::sqrt(len_sq);
    return Quat{.x = q.x * inv_len, .y = q.y * inv_len, .z = q.z * inv_len, .w = q.w * inv_len};
}

std::string asset_kind_name(const AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture:
            return "texture";
        case AssetKind::Mesh:
            return "mesh";
        case AssetKind::Material:
            return "material";
        case AssetKind::Model:
            return "model";
    }
    return "asset";
}
}  // namespace

void AssetRegistry::clear() {
    textures_.clear();
    meshes_.clear();
    materials_.clear();
    models_.clear();
    texture_resolver_  = {};
    mesh_resolver_     = {};
    material_resolver_ = {};
    model_resolver_    = {};
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

void AssetRegistry::register_model(const AssetHandle handle,
                                   std::string asset_id,
                                   const int runtime_id,
                                   const bool materialized) {
    register_asset(AssetKind::Model, handle, std::move(asset_id), runtime_id, materialized);
}

std::string AssetRegistry::asset_id_for_runtime(const AssetKind kind, const int runtime_id) const {
    for (const auto& [handle, record] : map_for(kind)) {
        if (record.runtime_id == runtime_id) {
            return record.asset_id;
        }
    }
    return {};
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
            .asset_id   = it->second.asset_id,
        };
    }

    auto& resolver = resolver_for(kind);
    if (resolver) {
        if (auto loaded = resolver(handle); loaded.has_value()) {
            AssetRecord record  = std::move(*loaded);
            record.handle       = handle;
            record.kind         = kind;
            auto [it, inserted] = assets.emplace(handle, std::move(record));
            (void)inserted;
            return AssetResolution{
                .handle     = handle,
                .kind       = kind,
                .status     = it->second.materialized ? AssetStatus::Materialized : AssetStatus::Registered,
                .runtime_id = it->second.runtime_id,
                .asset_id   = it->second.asset_id,
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
        case AssetKind::Model:
            return models_;
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
        case AssetKind::Model:
            return models_;
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
        case AssetKind::Model:
            return model_resolver_;
    }
    return texture_resolver_;
}

AssetRegistry& shared_asset_registry() noexcept try {
    static AssetRegistry registry;
    return registry;
} catch (...) {
    std::terminate();
}

namespace stdlib::math {

float sqrt(float v) noexcept {
    return std::sqrt(v);
}

float sin(float a) noexcept {
    return cr_sinf(a);
}

float cos(float a) noexcept {
    return cr_cosf(a);
}

float atan2(float y, float x) noexcept {
    return cr_atan2f(y, x);
}

int floor(float v) noexcept {
    return num::to_int(std::floor(v));
}

int ceil(float v) noexcept {
    return num::to_int(std::ceil(v));
}

int round(float v) noexcept {
    return num::to_int(std::round(v));
}

float pow(float base, float exp) noexcept {
    return cr_powf(base, exp);
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
    return stdlib::math::atan2(v.y, v.x);
}

Vector2 rotate(Vector2 v, float angle) noexcept {
    const float cos_a = stdlib::math::cos(angle);
    const float sin_a = stdlib::math::sin(angle);
    return Vector2{.x = (v.x * cos_a) - (v.y * sin_a), .y = (v.x * sin_a) + (v.y * cos_a)};
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

Vector3 rotate(Vector3 v, Vector3 axis, float angle) noexcept {
    return quat::rotate(quat::from_axis_angle(axis, angle), v);
}

}  // namespace vec3

namespace quat {

// Same formulas as raymath's QuaternionFromEuler/FromAxisAngle, on the deterministic sin/cos.
Quat from_euler(float pitch, float yaw, float roll) noexcept {
    const float x0 = stdlib::math::cos(pitch * 0.5F);
    const float x1 = stdlib::math::sin(pitch * 0.5F);
    const float y0 = stdlib::math::cos(yaw * 0.5F);
    const float y1 = stdlib::math::sin(yaw * 0.5F);
    const float z0 = stdlib::math::cos(roll * 0.5F);
    const float z1 = stdlib::math::sin(roll * 0.5F);
    return Quat{
        .x = (x1 * y0 * z0) - (x0 * y1 * z1),
        .y = (x0 * y1 * z0) + (x1 * y0 * z1),
        .z = (x0 * y0 * z1) - (x1 * y1 * z0),
        .w = (x0 * y0 * z0) + (x1 * y1 * z1),
    };
}

Quat from_axis_angle(Vector3 axis, float angle) noexcept {
    const float length = vec3::length(axis);
    if (length == 0.0F) {
        return identity();
    }
    const float inv_length = 1.0F / length;
    const float half       = angle * 0.5F;
    const float sin_half   = stdlib::math::sin(half);
    return normalize_or_identity(Quat{.x = axis.x * inv_length * sin_half,
                                      .y = axis.y * inv_length * sin_half,
                                      .z = axis.z * inv_length * sin_half,
                                      .w = stdlib::math::cos(half)});
}

Vector3 forward(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{.x = 0.0F, .y = 0.0F, .z = -1.0F}, normalize_or_identity(q));
}

Vector3 right(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, normalize_or_identity(q));
}

Vector3 up(Quat q) noexcept {
    return Vector3RotateByQuaternion(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, normalize_or_identity(q));
}

Vector3 rotate(Quat q, Vector3 v) noexcept {
    return Vector3RotateByQuaternion(v, normalize_or_identity(q));
}

// raymath's QuaternionSlerp, with acos(c) written as atan2(sqrt(1 - c*c), c) on the deterministic atan2.
Quat slerp(Quat a, Quat b, float t) noexcept {
    const float amount = stdlib::math::clamp(t, 0.0F, 1.0F);
    const Quat from    = normalize_or_identity(a);
    Quat to            = normalize_or_identity(b);
    float cos_half     = dot(from, to);
    if (cos_half < 0.0F) {
        to       = Quat{.x = -to.x, .y = -to.y, .z = -to.z, .w = -to.w};
        cos_half = -cos_half;
    }
    if (cos_half >= 1.0F) {
        return from;
    }
    if (cos_half > 0.95F) {
        return QuaternionNlerp(from, to, amount);
    }
    const float sin_half = std::sqrt(1.0F - (cos_half * cos_half));
    if (sin_half < 0.000001F) {
        return Quat{.x = (from.x * 0.5F) + (to.x * 0.5F),
                    .y = (from.y * 0.5F) + (to.y * 0.5F),
                    .z = (from.z * 0.5F) + (to.z * 0.5F),
                    .w = (from.w * 0.5F) + (to.w * 0.5F)};
    }
    const float half_theta = stdlib::math::atan2(sin_half, cos_half);
    const float ratio_a    = stdlib::math::sin((1.0F - amount) * half_theta) / sin_half;
    const float ratio_b    = stdlib::math::sin(amount * half_theta) / sin_half;
    return Quat{.x = (from.x * ratio_a) + (to.x * ratio_b),
                .y = (from.y * ratio_a) + (to.y * ratio_b),
                .z = (from.z * ratio_a) + (to.z * ratio_b),
                .w = (from.w * ratio_a) + (to.w * ratio_b)};
}

Quat inverse(Quat q) noexcept {
    if (length_squared(q) <= 0.0F) {
        return identity();
    }
    return QuaternionInvert(q);
}

Quat normalize(Quat value) noexcept {
    return normalize_or_identity(value);
}

Quat compose(Quat outer, Quat inner) noexcept {
    return normalize_or_identity(multiply(outer, inner));
}

Quat rotate_local(Quat current, Quat delta) noexcept {
    return compose(current, delta);
}

Quat rotate_world(Quat current, Quat delta) noexcept {
    return compose(delta, current);
}

bool same_rotation(Quat a, Quat b, float tolerance) noexcept {
    if (tolerance < 0.0F) {
        return false;
    }
    const Quat normalized_a = normalize_or_identity(a);
    const Quat normalized_b = normalize_or_identity(b);
    const float cos_angle   = dot(normalized_a, normalized_b);
    return (1.0F - std::abs(cos_angle)) <= tolerance;
}

}  // namespace quat

}  // namespace stdlib::math

namespace stdlib::random {

Rng seeded(int s) noexcept try {
    std::seed_seq seq{static_cast<std::uint32_t>(s)};
    std::minstd_rand engine{seq};
    return Rng{static_cast<int>(engine())};
} catch (...) {
    std::terminate();
}

Uniform uniform(float lo, float hi) noexcept {
    return Uniform{.lo = lo, .hi = hi};
}

UniformInt uniform_int(int lo, int hi) noexcept {
    return UniformInt{.lo = lo, .hi = hi};
}

Normal normal(float mean, float stddev) noexcept {
    return Normal{.mean = mean, .stddev = stddev};
}

Rng advance(Rng rng) noexcept {
    std::minstd_rand engine{static_cast<std::minstd_rand::result_type>(rng.state)};
    return Rng{static_cast<int>(engine())};
}

float sample(Rng rng, Uniform dist) noexcept {
    std::minstd_rand engine{static_cast<std::minstd_rand::result_type>(rng.state)};
    return std::uniform_real_distribution<float>{dist.lo, dist.hi}(engine);
}

int sample_int(Rng rng, UniformInt dist) noexcept {
    std::minstd_rand engine{static_cast<std::minstd_rand::result_type>(rng.state)};
    return std::uniform_int_distribution<int>{dist.lo, dist.hi}(engine);
}

float sample_normal(Rng rng, Normal dist) noexcept {
    std::minstd_rand engine{static_cast<std::minstd_rand::result_type>(rng.state)};
    return std::normal_distribution<float>{dist.mean, dist.stddev}(engine);
}

bool chance(Rng rng, float p) noexcept {
    if (p >= 1.0F) {
        return true;
    }
    if (p <= 0.0F) {
        return false;
    }
    std::minstd_rand engine{static_cast<std::minstd_rand::result_type>(rng.state)};
    return std::bernoulli_distribution{static_cast<double>(p)}(engine);
}

Color palette_color(int index) noexcept {
    static constexpr std::array<Color, 8> kPalette{{
        {.r = 255, .g = 107, .b = 107, .a = 255},
        {.r = 78, .g = 205, .b = 196, .a = 255},
        {.r = 255, .g = 217, .b = 61, .a = 255},
        {.r = 107, .g = 203, .b = 119, .a = 255},
        {.r = 77, .g = 150, .b = 255, .a = 255},
        {.r = 179, .g = 136, .b = 255, .a = 255},
        {.r = 255, .g = 146, .b = 43, .a = 255},
        {.r = 240, .g = 101, .b = 149, .a = 255},
    }};
    const int remainder = ((index % 8) + 8) % 8;
    return kPalette[static_cast<std::size_t>(remainder)];
}

}  // namespace stdlib::random

}  // namespace cactus::runtime