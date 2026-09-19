#pragma once

#include <bit>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <type_traits>

// Little-endian binary I/O primitives shared by every binary format this
// compiler/runtime reads or writes on disk (the module-artifact .cmod
// serializer and the example persistence file adapter) — one wire-format
// vocabulary instead of independently re-derived copies that could drift.

namespace cactus::binary_io {

template <typename T>
void write_uint(std::ostream& out, T value) {
    static_assert(std::is_unsigned_v<T>, "write_uint requires an unsigned integer type");
    for (unsigned shift = 0; shift < sizeof(T) * 8U; shift += 8U) {
        out.put(static_cast<char>((value >> shift) & 0xFFU));
    }
}

template <typename T>
[[nodiscard]] T read_uint(std::istream& in) {
    static_assert(std::is_unsigned_v<T>, "read_uint requires an unsigned integer type");
    T value = 0;
    for (unsigned shift = 0; shift < sizeof(T) * 8U; shift += 8U) {
        value |= static_cast<T>(static_cast<std::uint8_t>(in.get())) << shift;
    }
    return value;
}

inline void write_float(std::ostream& out, float value) {
    write_uint(out, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] inline float read_float(std::istream& in) {
    return std::bit_cast<float>(read_uint<std::uint32_t>(in));
}

inline void write_string(std::ostream& out, const std::string& value) {
    write_uint(out, static_cast<std::uint32_t>(value.size()));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

// A malformed or corrupted length prefix must not translate into an unbounded
// allocation attempt; a length past `max_length` sets the stream's failbit
// and returns an empty string instead of trusting the file.
[[nodiscard]] inline std::string read_string(std::istream& in, std::uint32_t max_length = 1024U * 1024U) {
    const auto length = read_uint<std::uint32_t>(in);
    if (length > max_length) {
        in.setstate(std::ios::failbit);
        return {};
    }
    std::string value(length, '\0');
    in.read(value.data(), static_cast<std::streamsize>(length));
    return value;
}

}  // namespace cactus::binary_io
