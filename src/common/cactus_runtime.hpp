#pragma once

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

// vec2/vec3 constructors and the scalar*vector operator overloads below live
// in the global namespace, not cactus::runtime: Vector2/Vector3 are plain C
// structs from raylib with no associated namespace, so ADL only finds
// operators/constructors for them here - matching the convention raymath.h's
// own C++ operator overloads (+, -, vec*float, vec*vec, vec/float, vec/vec,
// and their compound forms), included above, already use. Reaching this
// header therefore also makes those raymath operators reachable from
// generated handler code, which they are not otherwise.

// vec2/vec3 component constructors: the callee generated code invokes for
// every `vec2(x, y)`/`vec3(x, y, z)` call site. Previously emitted as
// per-program codegen text (cpp_entt_codegen.cpp); identical for every
// generated program, so it belongs here instead. Same signatures, same
// behavior as the codegen text they replace.
[[nodiscard]] inline Vector2 vec2(float x, float y) {
    return Vector2{.x = x, .y = y};
}
[[nodiscard]] inline Vector3 vec3(float x, float y, float z) {
    return Vector3{.x = x, .y = y, .z = z};
}
// Scalar splat constructors (dsl-vector-expressions): vec2(s) == vec2(s, s),
// vec3(s) == vec3(s, s, s).
[[nodiscard]] inline Vector2 vec2(float s) {
    return vec2(s, s);
}
[[nodiscard]] inline Vector3 vec3(float s) {
    return vec3(s, s, s);
}

// raymath.h defines `operator*(Vector2, float)`/`operator*(Vector3, float)`
// but not the commutative scalar-first form; the dsl-vector-expressions
// operator matrix accepts both orders, so add the one row raymath doesn't.
// Not constexpr: delegates to raymath.h's own (non-constexpr) operator*.
[[nodiscard]] inline Vector2 operator*(float scalar, const Vector2& v) {
    return v * scalar;
}
[[nodiscard]] inline Vector3 operator*(float scalar, const Vector3& v) {
    return v * scalar;
}

// `color` component access/constructor/operator matrix (dsl-vector-expressions
// "color"): declared here (not cactus::runtime) for the same ordinary-lookup
// reason as vec2/vec3 above — Color is a plain raylib C struct with no
// namespace, and generated call/operator text is unqualified. Defined out of
// line in cactus_runtime.cpp (also at global scope there) since, unlike
// vec2/vec3's trivial aggregate-literal constructors, it does real clamp/
// quantize work worth keeping in exactly one compiled place.
[[nodiscard]] Color color_from_components(float r, float g, float b, float a) noexcept;

// Generated code wraps a conditional nested in another conditional in this call: the inner one
// stays lazy, and generated code passes the nested-conditional lint.
template <typename T>
[[nodiscard]] constexpr T cactus_chosen(T value) {
    return value;
}

// Every color-constructing operator row (dsl-vector-expressions' fixed color
// binary operator matrix) normalizes both operands to [0,1] float, combines,
// and repacks through color_from_components — one clamp-then-quantize call
// site per row, never inlined per codegen call site (CLAUDE.md "Avoid
// duplication"; mirrors tint_with_opacity's now-shared implementation).
[[nodiscard]] inline Color operator+(const Color a, const Color b) {
    auto ch = [](const unsigned char v) { return static_cast<float>(v) / 255.0F; };
    return color_from_components(ch(a.r) + ch(b.r), ch(a.g) + ch(b.g), ch(a.b) + ch(b.b), ch(a.a) + ch(b.a));
}
[[nodiscard]] inline Color operator-(const Color a, const Color b) {
    auto ch = [](const unsigned char v) { return static_cast<float>(v) / 255.0F; };
    return color_from_components(ch(a.r) - ch(b.r), ch(a.g) - ch(b.g), ch(a.b) - ch(b.b), ch(a.a) - ch(b.a));
}
[[nodiscard]] inline Color operator*(const Color a, const float scalar) {
    auto ch = [](const unsigned char v) { return static_cast<float>(v) / 255.0F; };
    return color_from_components(ch(a.r) * scalar, ch(a.g) * scalar, ch(a.b) * scalar, ch(a.a) * scalar);
}
[[nodiscard]] inline Color operator*(const float scalar, const Color a) {
    return a * scalar;
}
[[nodiscard]] inline Color operator/(const Color a, const float scalar) {
    auto ch = [](const unsigned char v) { return static_cast<float>(v) / 255.0F; };
    return color_from_components(ch(a.r) / scalar, ch(a.g) / scalar, ch(a.b) / scalar, ch(a.a) / scalar);
}
[[nodiscard]] inline Color operator*(const Color a, const Color b) {
    auto ch = [](const unsigned char v) { return static_cast<float>(v) / 255.0F; };
    return color_from_components(ch(a.r) * ch(b.r), ch(a.g) * ch(b.g), ch(a.b) * ch(b.b), ch(a.a) * ch(b.a));
}

// Total `int` arithmetic: wraps modulo 2^32; `/` and `%` floor, and `x / 0 == 0`, `x % 0 == x`.
namespace cactus::num {

[[nodiscard]] constexpr int add(int a, int b) noexcept {
    return static_cast<int>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr int sub(int a, int b) noexcept {
    return static_cast<int>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr int mul(int a, int b) noexcept {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr int neg(int a) noexcept {
    return static_cast<int>(0U - static_cast<std::uint32_t>(a));
}
[[nodiscard]] constexpr int div(int a, int b) noexcept {
    if (b == 0) {
        return 0;
    }
    if (b == -1) {
        return neg(a);
    }
    const int quotient = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? quotient - 1 : quotient;
}
[[nodiscard]] constexpr int mod(int a, int b) noexcept {
    if (b == 0) {
        return a;
    }
    if (b == -1) {
        return 0;
    }
    const int remainder = a % b;
    return (remainder != 0 && ((remainder < 0) != (b < 0))) ? remainder + b : remainder;
}

// For operands the Cactus compiler did not type: an `int` pair picks the total overloads above,
// and every other type keeps its own operator.
template <typename A, typename B>
[[nodiscard]] constexpr auto add(A a, B b) noexcept {
    return a + b;
}
template <typename A, typename B>
[[nodiscard]] constexpr auto sub(A a, B b) noexcept {
    return a - b;
}
template <typename A, typename B>
[[nodiscard]] constexpr auto mul(A a, B b) noexcept {
    return a * b;
}
template <typename A, typename B>
[[nodiscard]] constexpr auto div(A a, B b) noexcept {
    return a / b;
}
template <typename A, typename B>
[[nodiscard]] constexpr auto mod(A a, B b) noexcept {
    return a % b;
}
template <typename A>
[[nodiscard]] constexpr auto neg(A a) noexcept {
    return -a;
}

// An already-rounded float as an `int`: NaN gives 0 and out-of-range values clamp.
[[nodiscard]] constexpr int to_int(float rounded) noexcept {
    if (rounded != rounded) {
        return 0;
    }
    if (rounded >= 2147483648.0F) {
        return std::numeric_limits<int>::max();
    }
    if (rounded <= -2147483648.0F) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(rounded);
}

// Sort-key order as -1, 0 or 1: NaN sorts after every number in both directions, NaNs tie, -0.0 ties 0.0.
template <typename T>
[[nodiscard]] constexpr int key_order(T lhs, T rhs, bool descending) noexcept {
    if constexpr (std::is_floating_point_v<T>) {
        const bool lhs_nan = lhs != lhs;
        const bool rhs_nan = rhs != rhs;
        if (lhs_nan || rhs_nan) {
            return static_cast<int>(lhs_nan) - static_cast<int>(rhs_nan);
        }
    }
    if (lhs == rhs) {
        return 0;
    }
    const bool before = descending ? rhs < lhs : lhs < rhs;
    return before ? -1 : 1;
}

static_assert(add(std::numeric_limits<int>::max(), 1) == std::numeric_limits<int>::min());
static_assert(neg(std::numeric_limits<int>::min()) == std::numeric_limits<int>::min());
static_assert(div(-7, 2) == -4 && mod(-7, 2) == 1);
static_assert(div(5, 0) == 0 && mod(5, 0) == 5);
static_assert(div(std::numeric_limits<int>::min(), -1) == std::numeric_limits<int>::min());

}  // namespace cactus::num

namespace cactus::runtime {

using Quat        = Quaternion;
using AssetHandle = std::uint32_t;

enum class AssetKind : std::uint8_t {
    Texture,
    Mesh,
    Material,
    Model,
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
    std::string_view asset_id;

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
    void register_model(AssetHandle handle, std::string asset_id, int runtime_id, bool materialized = true);

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
    AssetMap models_;
    LazyResolver texture_resolver_;
    LazyResolver mesh_resolver_;
    LazyResolver material_resolver_;
    LazyResolver model_resolver_;
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
    return GeneratedProjectInfo{.backend = backend, .project_name = project_name};
}

// `reduce:` fold steps. Sums don't depend on row order: `int` wraps and `float` is exact. Float
// min/max return NaN once any input is NaN.
namespace reduce {

template <typename T>
class Sum;

template <>
class Sum<int> {
public:
    constexpr void add(int input) noexcept {
        total_ = num::add(total_, input);
    }
    constexpr void merge(const Sum& other) noexcept {
        add(other.total_);
    }
    [[nodiscard]] constexpr int value() const noexcept {
        return total_;
    }

private:
    int total_ = 0;
};

// Every finite float is a multiple of 2^-149 below 2^128, so a 320-bit two's-complement fixed-point
// integer holds the exact sum of up to 2^42 inputs. value() rounds it once, to nearest even.
template <>
class Sum<float> {
public:
    constexpr void add(float input) noexcept {
        const auto bits          = std::bit_cast<std::uint32_t>(input);
        const bool negative      = (bits >> 31U) != 0U;
        const std::uint32_t exp  = (bits >> 23U) & 0xFFU;
        const std::uint32_t frac = bits & 0x7FFFFFU;
        seen_                    = true;
        only_negative_zero_      = only_negative_zero_ && bits == 0x80000000U;
        if (exp == 0xFFU) {
            nan_               = nan_ || frac != 0U;
            positive_infinity_ = positive_infinity_ || (frac == 0U && !negative);
            negative_infinity_ = negative_infinity_ || (frac == 0U && negative);
            return;
        }
        const std::uint64_t significand = exp == 0U ? frac : (frac | 0x800000U);
        const std::uint32_t shift       = exp == 0U ? 0U : exp - 1U;
        const std::size_t limb          = shift / 64U;
        const std::uint32_t offset      = shift % 64U;
        const std::uint64_t low         = significand << offset;
        const std::uint64_t high        = offset == 0U ? 0U : significand >> (64U - offset);
        if (negative) {
            subtract_at(limb, low);
            subtract_at(limb + 1U, high);
        } else {
            add_at(limb, low);
            add_at(limb + 1U, high);
        }
    }

    constexpr void merge(const Sum& other) noexcept {
        for (std::size_t i = 0; i < kLimbs; ++i) {
            add_at(i, other.limbs_[i]);
        }
        seen_               = seen_ || other.seen_;
        only_negative_zero_ = only_negative_zero_ && other.only_negative_zero_;
        nan_                = nan_ || other.nan_;
        positive_infinity_  = positive_infinity_ || other.positive_infinity_;
        negative_infinity_  = negative_infinity_ || other.negative_infinity_;
    }

    [[nodiscard]] constexpr float value() const noexcept {
        if (nan_ || (positive_infinity_ && negative_infinity_)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        if (positive_infinity_ || negative_infinity_) {
            return positive_infinity_ ? std::numeric_limits<float>::infinity()
                                      : -std::numeric_limits<float>::infinity();
        }
        const bool negative     = (limbs_[kLimbs - 1U] >> 63U) != 0U;
        const auto magnitude    = negative ? negated(limbs_) : limbs_;
        const std::uint32_t top = top_bit(magnitude);
        const std::uint32_t sign = negative ? 0x80000000U : 0U;
        if (top == kNoBit) {
            return seen_ && only_negative_zero_ ? -0.0F : 0.0F;
        }
        if (top <= 23U) {
            return std::bit_cast<float>(sign | static_cast<std::uint32_t>(magnitude[0]));
        }
        std::uint32_t low_bit = top - 23U;
        std::uint64_t kept    = bits_at(magnitude, low_bit);
        const bool guard      = bit_set(magnitude, low_bit - 1U);
        if (guard && (any_below(magnitude, low_bit - 1U) || (kept & 1U) != 0U)) {
            ++kept;
        }
        if (kept == (1U << 24U)) {
            kept >>= 1U;
            ++low_bit;
        }
        const std::uint32_t biased = low_bit + 1U;
        if (biased >= 0xFFU) {
            return std::bit_cast<float>(sign | 0x7F800000U);
        }
        return std::bit_cast<float>(sign | (biased << 23U) | (static_cast<std::uint32_t>(kept) & 0x7FFFFFU));
    }

private:
    static constexpr std::size_t kLimbs   = 5;
    static constexpr std::uint32_t kNoBit = 0xFFFFFFFFU;
    using Limbs                           = std::array<std::uint64_t, kLimbs>;

    constexpr void add_at(std::size_t index, std::uint64_t carry) noexcept {
        for (; index < kLimbs && carry != 0U; ++index) {
            limbs_[index] += carry;
            carry = limbs_[index] < carry ? 1U : 0U;
        }
    }
    constexpr void subtract_at(std::size_t index, std::uint64_t borrow) noexcept {
        for (; index < kLimbs && borrow != 0U; ++index) {
            const std::uint64_t before = limbs_[index];
            limbs_[index] -= borrow;
            borrow = before < borrow ? 1U : 0U;
        }
    }
    [[nodiscard]] static constexpr Limbs negated(Limbs limbs) noexcept {
        std::uint64_t carry = 1U;
        for (auto& limb : limbs) {
            limb  = ~limb + carry;
            carry = (carry != 0U && limb == 0U) ? 1U : 0U;
        }
        return limbs;
    }
    [[nodiscard]] static constexpr std::uint32_t top_bit(const Limbs& limbs) noexcept {
        for (std::size_t i = kLimbs; i-- > 0U;) {
            if (limbs[i] != 0U) {
                return static_cast<std::uint32_t>((i * 64U) + 63U - static_cast<std::size_t>(std::countl_zero(limbs[i])));
            }
        }
        return kNoBit;
    }
    [[nodiscard]] static constexpr bool bit_set(const Limbs& limbs, std::uint32_t position) noexcept {
        return ((limbs[position / 64U] >> (position % 64U)) & 1U) != 0U;
    }
    // The 24 bits starting at `position`.
    [[nodiscard]] static constexpr std::uint64_t bits_at(const Limbs& limbs, std::uint32_t position) noexcept {
        const std::size_t limb      = position / 64U;
        const std::uint32_t offset  = position % 64U;
        std::uint64_t bits          = limbs[limb] >> offset;
        if (offset > 40U && limb + 1U < kLimbs) {
            bits |= limbs[limb + 1U] << (64U - offset);
        }
        return bits & 0xFFFFFFU;
    }
    // Whether any bit below `position` is set.
    [[nodiscard]] static constexpr bool any_below(const Limbs& limbs, std::uint32_t position) noexcept {
        const std::size_t limb = position / 64U;
        for (std::size_t i = 0; i < limb; ++i) {
            if (limbs[i] != 0U) {
                return true;
            }
        }
        const std::uint32_t offset = position % 64U;
        return offset != 0U && (limbs[limb] & ((std::uint64_t{1} << offset) - 1U)) != 0U;
    }

    Limbs limbs_{};
    bool seen_               = false;
    bool only_negative_zero_ = true;
    bool nan_                = false;
    bool positive_infinity_  = false;
    bool negative_infinity_  = false;
};

template <>
class Sum<Vector2> {
public:
    constexpr void add(Vector2 input) noexcept {
        x_.add(input.x);
        y_.add(input.y);
    }
    constexpr void merge(const Sum& other) noexcept {
        x_.merge(other.x_);
        y_.merge(other.y_);
    }
    [[nodiscard]] constexpr Vector2 value() const noexcept {
        return Vector2{.x = x_.value(), .y = y_.value()};
    }

private:
    Sum<float> x_;
    Sum<float> y_;
};

template <>
class Sum<Vector3> {
public:
    constexpr void add(Vector3 input) noexcept {
        x_.add(input.x);
        y_.add(input.y);
        z_.add(input.z);
    }
    constexpr void merge(const Sum& other) noexcept {
        x_.merge(other.x_);
        y_.merge(other.y_);
        z_.merge(other.z_);
    }
    [[nodiscard]] constexpr Vector3 value() const noexcept {
        return Vector3{.x = x_.value(), .y = y_.value(), .z = z_.value()};
    }

private:
    Sum<float> x_;
    Sum<float> y_;
    Sum<float> z_;
};

[[nodiscard]] constexpr int min(int acc, int value) noexcept {
    return std::min(acc, value);
}
[[nodiscard]] constexpr int max(int acc, int value) noexcept {
    return std::max(acc, value);
}
// -0 orders below +0, so both are commutative.
[[nodiscard]] constexpr float min(float acc, float value) noexcept {
    if (acc != acc || value != value) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (acc == value) {
        return (std::bit_cast<std::uint32_t>(acc) >> 31U) != 0U ? acc : value;
    }
    return std::min(acc, value);
}
[[nodiscard]] constexpr float max(float acc, float value) noexcept {
    if (acc != acc || value != value) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (acc == value) {
        return (std::bit_cast<std::uint32_t>(acc) >> 31U) != 0U ? value : acc;
    }
    return std::max(acc, value);
}
// `best`: a strictly higher key wins, so ties keep the earlier row; a NaN key never wins.
[[nodiscard]] constexpr bool improves(int key, int best, bool seen) noexcept {
    return !seen || key > best;
}
[[nodiscard]] inline bool improves(float key, float best, bool seen) noexcept {
    return !std::isnan(key) && (!seen || key > best);
}

}  // namespace reduce

// flush() sets each target to round(old + exact sum of its deltas), independent of write order.
// Keyed by address: durable component storage doesn't move before the activation commits.
class AccumulatedDeltas {
public:
    void add(float& target, float delta) {
        deltas_.push_back(Delta{.target = &target, .value = delta});
    }
    void subtract(float& target, float delta) {
        deltas_.push_back(Delta{.target = &target, .value = -delta});
    }
    void add(Vector2& target, Vector2 delta) {
        add(target.x, delta.x);
        add(target.y, delta.y);
    }
    void subtract(Vector2& target, Vector2 delta) {
        subtract(target.x, delta.x);
        subtract(target.y, delta.y);
    }
    void add(Vector3& target, Vector3 delta) {
        add(target.x, delta.x);
        add(target.y, delta.y);
        add(target.z, delta.z);
    }
    void subtract(Vector3& target, Vector3 delta) {
        subtract(target.x, delta.x);
        subtract(target.y, delta.y);
        subtract(target.z, delta.z);
    }

    void flush() {
        std::ranges::sort(deltas_, std::ranges::less{}, &Delta::target);
        for (auto delta = deltas_.begin(); delta != deltas_.end();) {
            float* const target = delta->target;
            reduce::Sum<float> sum;
            sum.add(*target);
            for (; delta != deltas_.end() && delta->target == target; ++delta) {
                sum.add(delta->value);
            }
            *target = sum.value();
        }
        deltas_.clear();
    }

private:
    struct Delta {
        float* target;
        float value;
    };
    // A flat list reused across activations: no hashing or allocation per write.
    std::vector<Delta> deltas_;
};

namespace stdlib::math {

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
    return a + ((b - a) * t);
}
[[nodiscard]] constexpr float clamp(float x, float lo, float hi) noexcept {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}
[[nodiscard]] constexpr float abs(float v) noexcept {
    return (v < 0.0F) ? -v : v;
}
[[nodiscard]] constexpr float min(float a, float b) noexcept {
    return reduce::min(a, b);
}
[[nodiscard]] constexpr float max(float a, float b) noexcept {
    return reduce::max(a, b);
}
[[nodiscard]] float sqrt(float v) noexcept;
[[nodiscard]] constexpr float radians(float degrees) noexcept {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}
[[nodiscard]] constexpr float degrees(float radians) noexcept {
    return radians * (180.0F / std::numbers::pi_v<float>);
}
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
[[nodiscard]] constexpr Vector2 clamp(Vector2 v, Vector2 lo, Vector2 hi) noexcept {
    return Vector2{.x = stdlib::math::clamp(v.x, lo.x, hi.x), .y = stdlib::math::clamp(v.y, lo.y, hi.y)};
}
[[nodiscard]] Vector2 rotate(Vector2 v, float angle) noexcept;
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
[[nodiscard]] constexpr Vector3 clamp(Vector3 v, Vector3 lo, Vector3 hi) noexcept {
    return Vector3{
        .x = stdlib::math::clamp(v.x, lo.x, hi.x),
        .y = stdlib::math::clamp(v.y, lo.y, hi.y),
        .z = stdlib::math::clamp(v.z, lo.z, hi.z),
    };
}
[[nodiscard]] Vector3 rotate(Vector3 v, Vector3 axis, float angle) noexcept;
}  // namespace vec3

namespace quat {
[[nodiscard]] constexpr Quat identity() noexcept {
    return Quat{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
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
[[nodiscard]] Quat normalize(Quat value) noexcept;
[[nodiscard]] constexpr float dot(Quat a, Quat b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
}
[[nodiscard]] Quat compose(Quat outer, Quat inner) noexcept;
[[nodiscard]] Quat rotate_local(Quat current, Quat delta) noexcept;
[[nodiscard]] Quat rotate_world(Quat current, Quat delta) noexcept;
[[nodiscard]] bool same_rotation(Quat a, Quat b, float tolerance) noexcept;
}  // namespace quat

}  // namespace stdlib::math

namespace stdlib::random {

struct Rng {
    int state{};
};

struct Uniform {
    float lo{};
    float hi{};
};

struct UniformInt {
    int lo{};
    int hi{};
};

struct Normal {
    float mean{};
    float stddev{};
};

[[nodiscard]] Rng seeded(int s) noexcept;
[[nodiscard]] Uniform uniform(float lo, float hi) noexcept;
[[nodiscard]] UniformInt uniform_int(int lo, int hi) noexcept;
[[nodiscard]] Normal normal(float mean, float stddev) noexcept;
[[nodiscard]] Rng advance(Rng rng) noexcept;
[[nodiscard]] float sample(Rng rng, Uniform dist) noexcept;
[[nodiscard]] int sample_int(Rng rng, UniformInt dist) noexcept;
[[nodiscard]] float sample_normal(Rng rng, Normal dist) noexcept;
[[nodiscard]] bool chance(Rng rng, float p) noexcept;
[[nodiscard]] Color palette_color(int index) noexcept;

}  // namespace stdlib::random

}  // namespace cactus::runtime