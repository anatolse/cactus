#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

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
