#include "cir/cir_text.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace cactus::cir {
namespace {

constexpr std::string_view HEX_DIGITS = "0123456789abcdef";

void append_unicode_escape(std::string& out, unsigned char byte) {
    out += "\\u00";
    out += HEX_DIGITS[(byte >> 4U) & 0x0FU];
    out += HEX_DIGITS[byte & 0x0FU];
}

}  // namespace

void append_json_escaped(std::string& out, std::string_view text) {
    for (const char character : text) {
        switch (character) {
            case '"':
                out += "\\\"";
                continue;
            case '\\':
                out += "\\\\";
                continue;
            case '\b':
                out += "\\b";
                continue;
            case '\f':
                out += "\\f";
                continue;
            case '\n':
                out += "\\n";
                continue;
            case '\r':
                out += "\\r";
                continue;
            case '\t':
                out += "\\t";
                continue;
            default:
                break;
        }
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U) {
            append_unicode_escape(out, byte);
        } else {
            out += character;
        }
    }
}

std::string json_escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    append_json_escaped(out, text);
    return out;
}

void append_dot_escaped(std::string& out, std::string_view text) {
    for (const char character : text) {
        switch (character) {
            case '"':
                out += "\\\"";
                continue;
            case '\\':
                out += "\\\\";
                continue;
            case '\n':
                out += "\\n";
                continue;
            case '\r':
                out += "\\r";
                continue;
            case '\t':
                out += "\\t";
                continue;
            default:
                break;
        }
        // Other control characters have no DOT escape and would corrupt the
        // file, so they collapse to a space.
        out += static_cast<unsigned char>(character) < 0x20U ? ' ' : character;
    }
}

std::string dot_escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    append_dot_escaped(out, text);
    return out;
}

void append_mermaid_escaped(std::string& out, std::string_view text) {
    for (const char character : text) {
        switch (character) {
            case '&':
                out += "&amp;";
                continue;
            case '"':
                out += "&quot;";
                continue;
            case '<':
                out += "&lt;";
                continue;
            case '>':
                out += "&gt;";
                continue;
            case '#':
                out += "&#35;";
                continue;
            default:
                break;
        }
        out += static_cast<unsigned char>(character) < 0x20U ? ' ' : character;
    }
}

std::string mermaid_escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    append_mermaid_escaped(out, text);
    return out;
}

void append_json_number(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        out += "null";
        return;
    }
    out.append(buffer.data(), result.ptr);
}

}  // namespace cactus::cir
