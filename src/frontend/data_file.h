#pragma once

#include "common/error_reporter.h"
#include "common/types.h"
#include "frontend/ast.h"
#include "frontend/semantic_analyzer.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cactus {

// ── Field value representation (evaluated at compile time) ──────────────────

struct FieldValue {
    enum class Tag : uint8_t {
        Int     = 0,
        Float   = 1,
        Bool    = 2,
        Color   = 3,
        Vec2    = 4,
        Vec3    = 5,
        Quat    = 6,
        EntityId = 7,
        Enum    = 8,
        List    = 9,   // skipped in data file
        Struct  = 10,  // skipped in data file
        Unknown = 255
    };

    Tag tag = Tag::Unknown;

    // Payloads (only one valid at a time, based on tag)
    int32_t  i32  = 0;
    float    f32  = 0.0f;
    bool     b    = false;
    struct { uint8_t r = 0, g = 0, bv = 0, a = 255; } color;
    struct { float x = 0.0f, y = 0.0f; } vec2;
    struct { float x = 0.0f, y = 0.0f, z = 0.0f; } vec3;
    struct { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; } quat;
    uint32_t entity_id = 0;
    uint32_t enum_idx  = 0;

    bool is_serializable() const {
        return tag != Tag::Unknown && tag != Tag::List && tag != Tag::Struct;
    }

    /// Serialized byte count for this field value type (0 if not serializable)
    size_t byte_size() const;
};

// ── Entity instance data record ──────────────────────────────────────────────

struct EntityInstanceData {
    std::string name;                                    // unit archetype name
    std::vector<std::pair<std::string, FieldValue>> fields;  // field name → value
    uint64_t trait_mask = 0;                             // initial active trait bitmask
};

// ── _data.bin file writer ────────────────────────────────────────────────────

/// Writes a `_data.bin` flat binary file containing all `unit` instances
/// from a compiled module. `template` declarations are excluded.
///
/// Format:
///   [magic "CDAT"][uint16 version][uint32 entity_count]
///   Per entity: [uint16 name_len][name][uint32 field_count]
///               Per field: [uint16 name_len][name][uint8 type_tag][value bytes]
///               [uint64 trait_mask]
class DataFileWriter {
public:
    static constexpr uint16_t CURRENT_VERSION = 1;
    static constexpr const char* MAGIC = "CDAT";

    DataFileWriter(const ProgramNode& ast,
                   const DecoratedProgram& decorated,
                   ErrorReporter& errors);

    /// Write the data file. Returns the list of entity instances for testing.
    std::vector<EntityInstanceData> write(const std::filesystem::path& build_dir,
                                          const std::string& module_name);

    /// Build entity records without writing to disk (for testing).
    std::vector<EntityInstanceData> build_records();

    /// Convert module_name to data filename: "levels.level1" → "levels.level1_data.bin"
    static std::filesystem::path data_filename(const std::string& module_name);

private:
    // ── Constant evaluation ─────────────────────────────────────────────────
    void collect_constants();
    void collect_enums();
    std::optional<FieldValue> eval_expr(const ExprNode& expr) const;
    static std::optional<FieldValue> eval_literal(const LiteralExpr& lit);
    std::optional<FieldValue> eval_ident(const IdentExpr& ident) const;
    std::optional<FieldValue> eval_call(const CallExpr& call) const;
    std::optional<FieldValue> eval_member(const MemberExpr& mem) const;
    std::optional<FieldValue> eval_unary(const UnaryExpr& un) const;

    // ── Trait bitmask helpers ───────────────────────────────────────────────
    void build_trait_bit_index();
    uint64_t compute_trait_mask(const ApplyBlock& apply) const;
    static FieldValue make_field_value(const ResolvedField& field, const std::optional<FieldValue>& config_val);

    // ── Binary write helpers ────────────────────────────────────────────────
    static void write_u8(std::ostream& out, uint8_t v);
    static void write_u16(std::ostream& out, uint16_t v);
    static void write_u32(std::ostream& out, uint32_t v);
    static void write_u64(std::ostream& out, uint64_t v);
    static void write_str_short(std::ostream& out, const std::string& s);  // uint16 len + bytes
    static void write_field_value(std::ostream& out, const FieldValue& fv);

    const ProgramNode&      ast_;
    const DecoratedProgram& decorated_;
    ErrorReporter&          errors_;

    // Constant evaluation tables
    std::unordered_map<std::string, FieldValue> const_map_;   // const name → value
    // enum_name → (variant_name → index)
    std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>> enum_map_;
    // trait name → bit index
    std::unordered_map<std::string, uint32_t> trait_bit_index_;
};

// ── _data.bin file reader ────────────────────────────────────────────────────

/// Reads a `_data.bin` file and returns entity instance records.
class DataFileReader {
public:
    static constexpr uint16_t CURRENT_VERSION = 1;
    static constexpr const char* MAGIC = "CDAT";

    explicit DataFileReader(ErrorReporter& errors);

    /// Read entity records from path. Returns empty on error or version mismatch.
    std::vector<EntityInstanceData> load(const std::filesystem::path& path);

private:
    static uint8_t read_u8(std::istream& in);
    static uint16_t read_u16(std::istream& in);
    static uint32_t read_u32(std::istream& in);
    static uint64_t read_u64(std::istream& in);
    static std::string read_str_short(std::istream& in);
    static FieldValue read_field_value(std::istream& in, FieldValue::Tag tag);

    ErrorReporter& errors_;
};

}  // namespace cactus
