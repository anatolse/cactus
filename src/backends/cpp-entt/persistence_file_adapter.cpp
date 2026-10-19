#include "backends/cpp-entt/persistence_file_adapter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

namespace {

// ── Versioned example-only binary encoding ──────────────────────────────────
// A magic/version header, then the document in declaration order. Counts and
// integers are LEB128 varints (signed ones zigzagged), vectors store only
// their lanes, and every name is interned: its first use writes the string,
// later uses write its index. A trait field whose value equals the same
// field's value in the previous record is one repeat byte. The format carries
// no compatibility promise across compiler versions.

constexpr std::array<char, 4> kMagic = {'C', 'P', 'S', 'T'};
constexpr std::uint32_t kFileFormat  = 2;
constexpr std::uint8_t kRepeatValue   = 0xFF;

// Value equality that tells -0.0 from 0.0, so a repeat never changes a stored bit.
bool same_bits(const persistence::Value& left, const persistence::Value& right) {
    if (left.kind() != right.kind()) {
        return false;
    }
    switch (left.kind()) {
        case PersistenceValueKind::Float:
            return std::bit_cast<std::uint32_t>(left.as_float()) == std::bit_cast<std::uint32_t>(right.as_float());
        case PersistenceValueKind::Vector: {
            const auto& a = left.as_vector();
            const auto& b = right.as_vector();
            return a.lane_kind == b.lane_kind && a.lanes == b.lanes &&
                   std::bit_cast<std::array<std::uint32_t, 4>>(a.components) ==
                       std::bit_cast<std::array<std::uint32_t, 4>>(b.components);
        }
        case PersistenceValueKind::Struct:
            return std::ranges::equal(left.as_struct().fields, right.as_struct().fields,
                                      [](const persistence::FieldValue& a, const persistence::FieldValue& b) {
                                          return a.name == b.name && same_bits(a.value, b.value);
                                      });
        case PersistenceValueKind::List:
            return std::ranges::equal(left.as_list().items, right.as_list().items, same_bits);
        default:
            return left == right;
    }
}

// A trait field slot: the trait's and field's name indexes and whether it is a
// construction or a persisted field.
std::uint64_t field_slot(std::uint64_t trait, std::uint64_t field, bool persisted) {
    return (trait << 33U) | (field << 1U) | (persisted ? 1U : 0U);
}

class Encoder {
public:
    std::string take() { return std::move(bytes_); }

    void raw(std::string_view text) { bytes_.append(text); }

    void u8(std::uint8_t value) { bytes_.push_back(static_cast<char>(value)); }

    void varint(std::uint64_t value) {
        while (value >= 0x80U) {
            u8(static_cast<std::uint8_t>(value | 0x80U));
            value >>= 7U;
        }
        u8(static_cast<std::uint8_t>(value));
    }

    void signed_varint(std::int64_t value) {
        varint((static_cast<std::uint64_t>(value) << 1U) ^ static_cast<std::uint64_t>(value >> 63));
    }

    void f32(float value) {
        const auto raw = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(raw >> shift));
        }
    }

    std::uint64_t name(const std::string& value) {
        const auto [found, inserted] = names_.try_emplace(value, names_.size());
        if (!inserted) {
            varint(found->second + 1);
            return found->second;
        }
        varint(0);
        varint(value.size());
        bytes_.append(value);
        return found->second;
    }

    // Whether `value` repeats the slot's previous value; otherwise it becomes that value.
    bool repeats(std::uint64_t slot, const persistence::Value& value) {
        const auto [last, unseen] = previous_.try_emplace(slot, value);
        if (!unseen && same_bits(last->second, value)) {
            return true;
        }
        last->second = value;
        return false;
    }

private:
    std::string bytes_;
    std::unordered_map<std::string, std::uint64_t> names_;
    std::unordered_map<std::uint64_t, persistence::Value> previous_;
};

class Decoder {
public:
    explicit Decoder(std::string_view bytes)
        : bytes_(bytes) {}

    std::uint8_t u8() {
        if (offset_ >= bytes_.size()) {
            throw std::runtime_error("persistence file adapter: truncated save");
        }
        return static_cast<std::uint8_t>(bytes_[offset_++]);
    }

    std::uint64_t varint() {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            const auto byte = u8();
            value |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
            if ((byte & 0x80U) == 0) {
                return value;
            }
        }
        throw std::runtime_error("persistence file adapter: malformed varint");
    }

    std::int64_t signed_varint() {
        const auto raw = varint();
        return static_cast<std::int64_t>(raw >> 1U) ^ -static_cast<std::int64_t>(raw & 1U);
    }

    std::uint32_t count() {
        const auto value = varint();
        if (value > bytes_.size()) {
            throw std::runtime_error("persistence file adapter: count exceeds the save size");
        }
        return static_cast<std::uint32_t>(value);
    }

    float f32() {
        std::uint32_t raw = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            raw |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return std::bit_cast<float>(raw);
    }

    // A name and its index.
    std::pair<std::string, std::uint64_t> indexed_name() {
        const auto reference = varint();
        if (reference > 0) {
            if (reference > names_.size()) {
                throw std::runtime_error("persistence file adapter: unknown name reference");
            }
            return {names_[reference - 1], reference - 1};
        }
        const auto size = count();
        if (bytes_.size() - offset_ < size) {
            throw std::runtime_error("persistence file adapter: truncated save");
        }
        names_.emplace_back(bytes_.substr(offset_, size));
        offset_ += size;
        return {names_.back(), names_.size() - 1};
    }

    std::string name() { return indexed_name().first; }

    [[nodiscard]] bool next_is(std::uint8_t byte) const {
        return offset_ < bytes_.size() && static_cast<std::uint8_t>(bytes_[offset_]) == byte;
    }

    [[nodiscard]] const persistence::Value& previous(std::uint64_t slot) const {
        const auto last = previous_.find(slot);
        if (last == previous_.end()) {
            throw std::runtime_error("persistence file adapter: repeat without a previous value");
        }
        return last->second;
    }

    void remember(std::uint64_t slot, const persistence::Value& value) { previous_[slot] = value; }

    void expect(std::string_view text) {
        for (const char expected : text) {
            if (static_cast<char>(u8()) != expected) {
                throw std::runtime_error("persistence file adapter: not a save file");
            }
        }
    }

private:
    std::string_view bytes_;
    std::size_t offset_ = 0;
    std::vector<std::string> names_;
    std::unordered_map<std::uint64_t, persistence::Value> previous_;
};

void write_value(Encoder& out, const persistence::Value& value);
persistence::Value read_value(Decoder& in, std::size_t depth = 0);

void write_entity_ref(Encoder& out, const persistence::EntityRef& ref) {
    out.varint(ref.id);
    out.u8(ref.present ? 1 : 0);
}

persistence::EntityRef read_entity_ref(Decoder& in) {
    const auto id      = static_cast<persistence::DocumentId>(in.varint());
    const auto present = in.u8() != 0;
    return persistence::EntityRef{.id = id, .present = present};
}

void write_fields(Encoder& out, const std::vector<persistence::FieldValue>& fields) {
    out.varint(fields.size());
    for (const auto& field : fields) {
        out.name(field.name);
        write_value(out, field.value);
    }
}

std::vector<persistence::FieldValue> read_fields(Decoder& in, std::size_t depth) {
    std::vector<persistence::FieldValue> fields(in.count());
    for (auto& field : fields) {
        field.name  = in.name();
        field.value = read_value(in, depth);
    }
    return fields;
}

void write_trait_fields(Encoder& out,
                        std::uint64_t trait,
                        bool persisted,
                        const std::vector<persistence::FieldValue>& fields) {
    out.varint(fields.size());
    for (const auto& field : fields) {
        if (out.repeats(field_slot(trait, out.name(field.name), persisted), field.value)) {
            out.u8(kRepeatValue);
            continue;
        }
        write_value(out, field.value);
    }
}

std::vector<persistence::FieldValue> read_trait_fields(Decoder& in, std::uint64_t trait, bool persisted) {
    std::vector<persistence::FieldValue> fields(in.count());
    for (auto& field : fields) {
        auto [name, index] = in.indexed_name();
        field.name         = std::move(name);
        const auto slot    = field_slot(trait, index, persisted);
        if (in.next_is(kRepeatValue)) {
            in.u8();
            field.value = in.previous(slot);
            continue;
        }
        field.value = read_value(in);
        in.remember(slot, field.value);
    }
    return fields;
}

void write_value(Encoder& out, const persistence::Value& value) {
    out.u8(static_cast<std::uint8_t>(value.kind()));
    switch (value.kind()) {
        case PersistenceValueKind::Bool:
            out.u8(value.as_bool() ? 1 : 0);
            return;
        case PersistenceValueKind::Int:
            out.signed_varint(value.as_int());
            return;
        case PersistenceValueKind::Float:
            out.f32(value.as_float());
            return;
        case PersistenceValueKind::Vector: {
            const auto& vector = value.as_vector();
            out.u8(static_cast<std::uint8_t>(vector.lane_kind));
            out.u8(vector.lanes);
            for (std::size_t lane = 0; lane < vector.lanes && lane < vector.components.size(); ++lane) {
                out.f32(vector.components[lane]);
            }
            return;
        }
        case PersistenceValueKind::String:
            out.name(value.as_string());
            return;
        case PersistenceValueKind::EntityRef:
            write_entity_ref(out, value.as_entity());
            return;
        case PersistenceValueKind::Enum:
            out.name(value.as_enum().type);
            out.name(value.as_enum().variant);
            return;
        case PersistenceValueKind::AssetRef:
        case PersistenceValueKind::InputRef:
            out.name(value.as_declaration());
            return;
        case PersistenceValueKind::Struct:
            write_fields(out, value.as_struct().fields);
            return;
        case PersistenceValueKind::List: {
            const auto& items = value.as_list().items;
            out.varint(items.size());
            for (const auto& item : items) {
                write_value(out, item);
            }
            return;
        }
        case PersistenceValueKind::Unsupported:
            return;  // absent: the kind byte alone is the whole payload
    }
}

persistence::Value read_value(Decoder& in, std::size_t depth) {
    // A crafted file could otherwise nest deep enough to overflow the stack.
    if (depth > persistence::kMaxPersistenceNestingDepth) {
        throw std::runtime_error("persistence file adapter: value nested too deeply");
    }
    const auto kind = static_cast<PersistenceValueKind>(in.u8());
    switch (kind) {
        case PersistenceValueKind::Bool:
            return persistence::Value::of_bool(in.u8() != 0);
        case PersistenceValueKind::Int:
            return persistence::Value::of_int(static_cast<std::int32_t>(in.signed_varint()));
        case PersistenceValueKind::Float:
            return persistence::Value::of_float(in.f32());
        case PersistenceValueKind::Vector: {
            const auto lane_kind = static_cast<PersistenceValueKind>(in.u8());
            const auto lanes     = in.u8();
            std::array<float, 4> components{};
            for (std::size_t lane = 0; lane < lanes && lane < components.size(); ++lane) {
                components[lane] = in.f32();
            }
            return persistence::Value::of_vector(lane_kind, lanes, components);
        }
        case PersistenceValueKind::String:
            return persistence::Value::of_string(in.name());
        case PersistenceValueKind::EntityRef:
            return persistence::Value::of_entity(read_entity_ref(in));
        case PersistenceValueKind::Enum: {
            auto type    = in.name();
            auto variant = in.name();
            return persistence::Value::of_enum(std::move(type), std::move(variant));
        }
        case PersistenceValueKind::AssetRef:
            return persistence::Value::of_asset(in.name());
        case PersistenceValueKind::InputRef:
            return persistence::Value::of_input(in.name());
        case PersistenceValueKind::Struct:
            return persistence::Value::of_struct(persistence::StructValue{.fields = read_fields(in, depth + 1)});
        case PersistenceValueKind::List: {
            persistence::ListValue value;
            value.items.resize(in.count());
            for (auto& item : value.items) {
                item = read_value(in, depth + 1);
            }
            return persistence::Value::of_list(std::move(value));
        }
        case PersistenceValueKind::Unsupported:
            return persistence::Value{};
    }
    throw std::runtime_error("persistence file adapter: unrecognized value kind byte");
}

void write_entity_record(Encoder& out, const persistence::EntityRecord& record) {
    out.varint(record.id);
    out.name(record.archetype);
    write_entity_ref(out, record.parent);
    out.varint(record.traits.size());
    for (const auto& trait : record.traits) {
        const auto id = out.name(trait.trait);
        out.u8(trait.present ? 1 : 0);
        write_trait_fields(out, id, false, trait.construction);
        write_trait_fields(out, id, true, trait.persisted);
    }
}

persistence::EntityRecord read_entity_record(Decoder& in) {
    persistence::EntityRecord record;
    record.id        = static_cast<persistence::DocumentId>(in.varint());
    record.archetype = in.name();
    record.parent    = read_entity_ref(in);
    record.traits.resize(in.count());
    for (auto& trait : record.traits) {
        auto [name, id]    = in.indexed_name();
        trait.trait        = std::move(name);
        trait.present      = in.u8() != 0;
        trait.construction = read_trait_fields(in, id, false);
        trait.persisted    = read_trait_fields(in, id, true);
    }
    return record;
}

std::string encode_snapshot(const persistence::Snapshot& snapshot) {
    Encoder out;
    out.raw(std::string_view(kMagic.data(), kMagic.size()));
    out.varint(kFileFormat);
    out.varint(snapshot.schema_revision);
    out.varint(snapshot.schema_fingerprint);
    out.name(snapshot.module);
    out.varint(snapshot.entities.size());
    for (const auto& record : snapshot.entities) {
        write_entity_record(out, record);
    }
    return out.take();
}

persistence::Snapshot decode_snapshot(std::string_view bytes) {
    Decoder in(bytes);
    in.expect(std::string_view(kMagic.data(), kMagic.size()));
    if (in.varint() != kFileFormat) {
        throw std::runtime_error("persistence file adapter: unsupported file format version");
    }
    persistence::Snapshot snapshot;
    snapshot.schema_revision    = static_cast<std::uint32_t>(in.varint());
    snapshot.schema_fingerprint = in.varint();
    snapshot.module             = in.name();
    snapshot.entities.resize(in.count());
    for (auto& record : snapshot.entities) {
        record = read_entity_record(in);
    }
    return snapshot;
}

std::filesystem::path slot_file_path(const std::filesystem::path& directory, const std::string& slot) {
    return directory / (slot + ".cactussave");
}

std::filesystem::path slot_temp_path(const std::filesystem::path& directory, const std::string& slot) {
    return directory / (slot + ".cactussave.tmp");
}

}  // namespace

PersistenceAdapter make_example_file_adapter(const std::filesystem::path& directory) {
    // Captured as a shared_ptr, not the path itself: std::function's internal
    // storage copies the closure, and copying a std::filesystem::path can
    // throw (e.g. bad_alloc) — copying a shared_ptr cannot.
    const auto shared_directory = std::make_shared<const std::filesystem::path>(directory);
    return PersistenceAdapter{
        .write =
            [shared_directory](const std::string& slot,
                               const persistence::SchemaDescriptor&,
                               const persistence::Snapshot& snapshot) -> PersistenceWriteResult {
                // The whole body is guarded, not just encode_snapshot: path
                // construction below can itself throw (e.g. bad_alloc), and an
                // adapter is never allowed to let an exception escape across
                // the ABI boundary (decision 7's explicit error contract).
                try {
                    const auto& directory = *shared_directory;
                    std::error_code ec;
                    std::filesystem::create_directories(directory, ec);

                    const auto temp_path = slot_temp_path(directory, slot);
                    {
                        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
                        if (!out) {
                            return PersistenceWriteResult{
                                .ok      = false,
                                .code    = "io_failure",
                                .message = "failed to open a temporary file for slot '" + slot + "'"};
                        }
                        try {
                            out.exceptions(std::ios::failbit | std::ios::badbit);
                            const auto bytes = encode_snapshot(snapshot);
                            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                        } catch (const std::exception&) {
                            out.close();
                            std::filesystem::remove(temp_path, ec);
                            return PersistenceWriteResult{
                                .ok = false, .code = "io_failure", .message = "failed to write slot '" + slot + "'"};
                        }
                    }

                    // Atomic commit: the previously saved slot file, if any, is
                    // only ever replaced by a fully-written document.
                    std::filesystem::rename(temp_path, slot_file_path(directory, slot), ec);
                    if (ec) {
                        std::filesystem::remove(temp_path, ec);
                        return PersistenceWriteResult{
                            .ok = false, .code = "io_failure", .message = "failed to commit slot '" + slot + "'"};
                    }
                    return PersistenceWriteResult{.ok = true};
                } catch (const std::exception&) {
                    return PersistenceWriteResult{
                        .ok = false, .code = "io_failure", .message = "failed to write slot '" + slot + "'"};
                }
            },
        .read =
            [shared_directory](const std::string& slot,
                               const persistence::SchemaDescriptor&) -> PersistenceReadResult {
                try {
                    const auto& directory = *shared_directory;
                    const auto path       = slot_file_path(directory, slot);
                    std::ifstream in(path, std::ios::binary);
                    if (!in) {
                        return PersistenceReadResult{
                            .ok = false, .code = "io_failure", .message = "no save found for slot '" + slot + "'"};
                    }
                    in.exceptions(std::ios::failbit | std::ios::badbit);
                    std::string bytes(static_cast<std::size_t>(std::filesystem::file_size(path)), '\0');
                    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                    return PersistenceReadResult{.ok = true, .snapshot = decode_snapshot(bytes)};
                } catch (const std::exception&) {
                    return PersistenceReadResult{
                        .ok = false, .code = "io_failure", .message = "failed to read slot '" + slot + "'"};
                }
            }};
}

}  // namespace cactus::runtime::entt_backend
