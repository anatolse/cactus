#include "common/duration.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <numeric>
#include <optional>

namespace cactus {

namespace {

struct Suffix {
    std::string_view name;
    std::uint64_t units;
};

constexpr std::array<Suffix, 4> kSuffixes{{
    {.name = "ms", .units = kDurationUnitsPerSecond / 1000},
    {.name = "min", .units = kDurationUnitsPerSecond * 60},
    {.name = "s", .units = kDurationUnitsPerSecond},
    {.name = "h", .units = kDurationUnitsPerSecond * 3600},
}};

// More decimals than this can't be whole: the largest suffix has 5^7 and 2^13 as factors.
constexpr std::size_t kMaxDecimals = 18;

bool identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::uint64_t power_of_ten(std::size_t exponent) {
    std::uint64_t value = 1;
    for (std::size_t i = 0; i < exponent; ++i) {
        value *= 10;
    }
    return value;
}

std::optional<std::uint64_t> parse_digits(std::string_view digits) {
    std::uint64_t value = 0;
    for (const char digit : digits) {
        if (__builtin_mul_overflow(value, 10U, &value) ||
            __builtin_add_overflow(value, static_cast<std::uint64_t>(digit - '0'), &value)) {
            return std::nullopt;
        }
    }
    return value;
}

// `whole.fraction` with `decimals` fraction digits, written as a literal with its suffix.
std::string literal_text(std::uint64_t whole, std::uint64_t fraction, std::size_t decimals, std::string_view suffix) {
    const auto scale = power_of_ten(decimals);
    whole += fraction / scale;
    fraction %= scale;
    auto text = std::to_string(whole);
    if (fraction != 0) {
        auto digits = std::to_string(fraction);
        digits.insert(0, decimals - digits.size(), '0');
        while (digits.back() == '0') {
            digits.pop_back();
        }
        text += "." + digits;
    }
    return "`" + text + std::string(suffix) + "`";
}

}  // namespace

std::size_t duration_suffix_length(std::string_view rest) noexcept {
    for (const auto& suffix : kSuffixes) {
        if (rest.starts_with(suffix.name) &&
            (rest.size() == suffix.name.size() || !identifier_char(rest[suffix.name.size()]))) {
            return suffix.name.size();
        }
    }
    return 0;
}

std::expected<std::int64_t, std::string> duration_literal_units(std::string_view text) {
    const auto number_end  = text.find_first_not_of("0123456789.");
    const auto number      = text.substr(0, number_end);
    const auto suffix_name = number_end == std::string_view::npos ? std::string_view{} : text.substr(number_end);
    const auto suffix =
        std::ranges::find_if(kSuffixes, [&](const Suffix& candidate) { return candidate.name == suffix_name; });
    const auto dot = number.find('.');
    auto whole     = number.substr(0, dot);
    auto fraction  = dot == std::string_view::npos ? std::string_view{} : number.substr(dot + 1);
    if (suffix == kSuffixes.end() || whole.empty()) {
        return std::unexpected("malformed duration literal `" + std::string(text) + "`");
    }
    while (!fraction.empty() && fraction.back() == '0') {
        fraction.remove_suffix(1);
    }
    constexpr auto kMax     = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto out_of_range = [&] {
        return std::unexpected("duration literal `" + std::string(text) +
                               "` is out of range; the largest that fits is " +
                               literal_text(kMax / suffix->units, 0, 0, suffix->name));
    };
    const auto whole_value = parse_digits(whole);
    std::uint64_t units    = 0;
    if (!whole_value.has_value() || __builtin_mul_overflow(*whole_value, suffix->units, &units) || units > kMax) {
        return out_of_range();
    }
    const auto decimals = std::min(fraction.size(), kMaxDecimals);
    const auto digits   = parse_digits(fraction.substr(0, decimals)).value_or(0);
    const auto scale    = power_of_ten(decimals);
    const auto common   = std::gcd(scale, suffix->units);
    const auto grid     = scale / common;
    if (fraction.size() > kMaxDecimals || digits % grid != 0) {
        const auto below = digits - (digits % grid);
        return std::unexpected("duration literal `" + std::string(text) +
                               "` is not a whole number of time units (1/705600000 s); the nearest literals that "
                               "fit are " +
                               literal_text(*whole_value, below, decimals, suffix->name) + " and " +
                               literal_text(*whole_value, below + grid, decimals, suffix->name));
    }
    units += (digits / grid) * (suffix->units / common);
    if (units > kMax) {
        return out_of_range();
    }
    return static_cast<std::int64_t>(units);
}

std::string duration_text(Duration d) {
    return std::format("{}s", static_cast<double>(d.units) / static_cast<double>(kDurationUnitsPerSecond));
}

}  // namespace cactus
