#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace cactus {

// One unit is 1/705,600,000 s: it divides a millisecond and the steps of 24-144 Hz.
inline constexpr std::int64_t kDurationUnitsPerSecond = 705'600'000;

// A whole number of time units; arithmetic wraps modulo 2^64, as `int` wraps modulo 2^32.
struct Duration {
    std::int64_t units{};

    friend constexpr bool operator==(Duration, Duration) noexcept            = default;
    friend constexpr std::strong_ordering operator<=>(Duration, Duration) noexcept = default;
};

namespace duration_detail {

[[nodiscard]] constexpr std::uint64_t bits(std::int64_t value) noexcept {
    return static_cast<std::uint64_t>(value);
}
[[nodiscard]] constexpr Duration wrap(std::uint64_t value) noexcept {
    return Duration{static_cast<std::int64_t>(value)};
}

}  // namespace duration_detail

[[nodiscard]] constexpr Duration operator+(Duration a, Duration b) noexcept {
    return duration_detail::wrap(duration_detail::bits(a.units) + duration_detail::bits(b.units));
}
[[nodiscard]] constexpr Duration operator-(Duration a, Duration b) noexcept {
    return duration_detail::wrap(duration_detail::bits(a.units) - duration_detail::bits(b.units));
}
[[nodiscard]] constexpr Duration operator-(Duration a) noexcept {
    return duration_detail::wrap(0U - duration_detail::bits(a.units));
}
[[nodiscard]] constexpr Duration operator*(Duration a, std::int64_t factor) noexcept {
    return duration_detail::wrap(duration_detail::bits(a.units) * duration_detail::bits(factor));
}
[[nodiscard]] constexpr Duration operator*(std::int64_t factor, Duration a) noexcept {
    return a * factor;
}
// Floor division, and zero for a zero divisor, like `int` division.
[[nodiscard]] constexpr Duration operator/(Duration a, std::int64_t divisor) noexcept {
    if (divisor == 0) {
        return Duration{};
    }
    if (divisor == -1) {
        return -a;
    }
    const std::int64_t quotient = a.units / divisor;
    const bool inexact          = a.units % divisor != 0;
    return Duration{inexact && ((a.units < 0) != (divisor < 0)) ? quotient - 1 : quotient};
}
// The ratio of two durations, and zero for a zero divisor.
[[nodiscard]] constexpr float operator/(Duration a, Duration b) noexcept {
    if (b.units == 0) {
        return 0.0F;
    }
    return static_cast<float>(static_cast<double>(a.units) / static_cast<double>(b.units));
}
constexpr Duration& operator+=(Duration& a, Duration b) noexcept {
    return a = a + b;
}
constexpr Duration& operator-=(Duration& a, Duration b) noexcept {
    return a = a - b;
}
constexpr Duration& operator*=(Duration& a, std::int64_t factor) noexcept {
    return a = a * factor;
}
constexpr Duration& operator/=(Duration& a, std::int64_t divisor) noexcept {
    return a = a / divisor;
}

// One side of a duration operator: a duration or an `int`.
using DurationOperand = std::variant<Duration, std::int64_t>;

// `a op b` with at least one duration side: a duration, a `float` for `d / d`, or nullopt for an
// operator or operand pair durations don't have.
[[nodiscard]] constexpr std::optional<std::variant<Duration, float>> apply_duration_operator(
    std::string_view op, DurationOperand a, DurationOperand b) noexcept {
    const auto* left  = std::get_if<Duration>(&a);
    const auto* right = std::get_if<Duration>(&b);
    if (left != nullptr && right != nullptr) {
        if (op == "+") {
            return *left + *right;
        }
        if (op == "-") {
            return *left - *right;
        }
        if (op == "/") {
            return *left / *right;
        }
        return std::nullopt;
    }
    if (left != nullptr && op == "*") {
        return *left * std::get<std::int64_t>(b);
    }
    if (left != nullptr && op == "/") {
        return *left / std::get<std::int64_t>(b);
    }
    if (right != nullptr && op == "*") {
        return std::get<std::int64_t>(a) * *right;
    }
    return std::nullopt;
}

// `duration(x)`: the nearest unit count to `x` seconds, halves away from zero; NaN gives zero.
[[nodiscard]] inline Duration duration_from_seconds(float seconds) noexcept {
    if (std::isnan(seconds)) {
        return Duration{};
    }
    const double units = static_cast<double>(seconds) * static_cast<double>(kDurationUnitsPerSecond);
    constexpr double kLimit = 9223372036854775808.0;
    if (units >= kLimit) {
        return Duration{std::numeric_limits<std::int64_t>::max()};
    }
    if (units <= -kLimit) {
        return Duration{std::numeric_limits<std::int64_t>::min()};
    }
    return Duration{static_cast<std::int64_t>(std::round(units))};
}

// `seconds(d)`: the count over the units per second, rounded once to binary32.
[[nodiscard]] constexpr float seconds(Duration d) noexcept {
    return static_cast<float>(static_cast<double>(d.units) / static_cast<double>(kDurationUnitsPerSecond));
}

// `d` in seconds as text, such as `1.5s`.
[[nodiscard]] std::string duration_text(Duration d);

// The units of a literal such as `5s`, `120ms`, `3min`, `2h` or `0.5s`, or the diagnostic when the
// literal is not a whole number of units or is out of range.
[[nodiscard]] std::expected<std::int64_t, std::string> duration_literal_units(std::string_view text);

// The length of the unit suffix at the start of `rest` (`ms`, `s`, `min`, `h`), or 0 when there is none.
[[nodiscard]] std::size_t duration_suffix_length(std::string_view rest) noexcept;

}  // namespace cactus
