#pragma once

#include <string>
#include <string_view>

/// Per-format text escaping for the CIR serializers, kept in one place so a fix
/// to one hostile-input case cannot land in only one writer.
namespace cactus::cir {

/// Appends `text` escaped for a JSON string body (without surrounding quotes).
/// UTF-8 continuation bytes pass through unchanged; control characters below
/// 0x20 become their short escape or a `\u00XX` sequence.
void append_json_escaped(std::string& out, std::string_view text);

[[nodiscard]] std::string json_escaped(std::string_view text);

/// Appends `value` as a JSON number, or `null` when it is not finite.
void append_json_number(std::string& out, double value);

/// Appends `text` escaped for a double-quoted DOT string (without the quotes).
void append_dot_escaped(std::string& out, std::string_view text);

[[nodiscard]] std::string dot_escaped(std::string_view text);

/// Appends `text` escaped for a quoted Mermaid label (without the quotes).
/// Mermaid reads labels as HTML, so the markup-significant characters become
/// entities and control characters are replaced with a space.
void append_mermaid_escaped(std::string& out, std::string_view text);

[[nodiscard]] std::string mermaid_escaped(std::string_view text);

}  // namespace cactus::cir
