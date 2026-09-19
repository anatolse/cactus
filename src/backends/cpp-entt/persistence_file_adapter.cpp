#include "backends/cpp-entt/persistence_file_adapter.hpp"
#include "common/binary_io.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace cactus::runtime::entt_backend {

namespace {

// ── Versioned example-only binary encoding ──────────────────────────────────
// Deliberately simple: a fixed magic/version header, then the document
// written field-by-field in declaration order. No compression, no varint
// packing — clarity over density, since this format is documented as
// example-only and carries no compatibility promise across compiler versions.
// Primitives shared with the module-artifact (.cmod) serializer live in
// common/binary_io.hpp; these are thin aliases so the call sites below stay
// unchanged.

constexpr std::array<char, 4> kMagic = {'C', 'P', 'S', 'T'};
constexpr std::uint32_t kFileFormat  = 1;

void write_u8(std::ostream& out, std::uint8_t value) {
    binary_io::write_uint(out, value);
}

std::uint8_t read_u8(std::istream& in) {
    return binary_io::read_uint<std::uint8_t>(in);
}

void write_u32(std::ostream& out, std::uint32_t value) {
    binary_io::write_uint(out, value);
}

std::uint32_t read_u32(std::istream& in) {
    return binary_io::read_uint<std::uint32_t>(in);
}

void write_u64(std::ostream& out, std::uint64_t value) {
    binary_io::write_uint(out, value);
}

std::uint64_t read_u64(std::istream& in) {
    return binary_io::read_uint<std::uint64_t>(in);
}

void write_f32(std::ostream& out, float value) {
    binary_io::write_float(out, value);
}

float read_f32(std::istream& in) {
    return binary_io::read_float(in);
}

void write_string(std::ostream& out, const std::string& value) {
    binary_io::write_string(out, value);
}

std::string read_string(std::istream& in) {
    return binary_io::read_string(in);
}

void write_entity_ref(std::ostream& out, const persistence::EntityRef& ref) {
    write_u32(out, ref.id);
    write_u8(out, ref.present ? 1 : 0);
}

persistence::EntityRef read_entity_ref(std::istream& in) {
    const auto id      = read_u32(in);
    const auto present = read_u8(in) != 0;
    return persistence::EntityRef{.id = id, .present = present};
}

void write_value(std::ostream& out, const persistence::Value& value);
persistence::Value read_value(std::istream& in);

void write_field(std::ostream& out, const persistence::FieldValue& field) {
    write_string(out, field.name);
    write_value(out, field.value);
}

persistence::FieldValue read_field(std::istream& in) {
    auto name = read_string(in);
    auto value = read_value(in);
    return persistence::FieldValue{.name = std::move(name), .value = std::move(value)};
}

void write_value(std::ostream& out, const persistence::Value& value) {
    write_u8(out, static_cast<std::uint8_t>(value.kind()));
    switch (value.kind()) {
        case PersistenceValueKind::Bool:
            write_u8(out, value.as_bool() ? 1 : 0);
            return;
        case PersistenceValueKind::Int:
            write_u32(out, std::bit_cast<std::uint32_t>(value.as_int()));
            return;
        case PersistenceValueKind::Float:
            write_f32(out, value.as_float());
            return;
        case PersistenceValueKind::Vector: {
            const auto& vector = value.as_vector();
            write_u8(out, static_cast<std::uint8_t>(vector.lane_kind));
            write_u8(out, vector.lanes);
            for (const float component : vector.components) {
                write_f32(out, component);
            }
            return;
        }
        case PersistenceValueKind::String:
            write_string(out, value.as_string());
            return;
        case PersistenceValueKind::EntityRef:
            write_entity_ref(out, value.as_entity());
            return;
        case PersistenceValueKind::Enum:
            write_string(out, value.as_enum().type);
            write_string(out, value.as_enum().variant);
            return;
        case PersistenceValueKind::AssetRef:
        case PersistenceValueKind::InputRef:
            write_string(out, value.as_declaration());
            return;
        case PersistenceValueKind::Struct: {
            const auto& fields = value.as_struct().fields;
            write_u32(out, static_cast<std::uint32_t>(fields.size()));
            for (const auto& field : fields) {
                write_field(out, field);
            }
            return;
        }
        case PersistenceValueKind::List: {
            const auto& items = value.as_list().items;
            write_u32(out, static_cast<std::uint32_t>(items.size()));
            for (const auto& item : items) {
                write_value(out, item);
            }
            return;
        }
        case PersistenceValueKind::Unsupported:
            return;  // absent: the kind byte alone is the whole payload
    }
}

persistence::Value read_value(std::istream& in) {
    const auto kind = static_cast<PersistenceValueKind>(read_u8(in));
    switch (kind) {
        case PersistenceValueKind::Bool:
            return persistence::Value::of_bool(read_u8(in) != 0);
        case PersistenceValueKind::Int:
            return persistence::Value::of_int(std::bit_cast<std::int32_t>(read_u32(in)));
        case PersistenceValueKind::Float:
            return persistence::Value::of_float(read_f32(in));
        case PersistenceValueKind::Vector: {
            const auto lane_kind = static_cast<PersistenceValueKind>(read_u8(in));
            const auto lanes     = read_u8(in);
            std::array<float, 4> components{};
            for (float& component : components) {
                component = read_f32(in);
            }
            return persistence::Value::of_vector(lane_kind, lanes, components);
        }
        case PersistenceValueKind::String:
            return persistence::Value::of_string(read_string(in));
        case PersistenceValueKind::EntityRef:
            return persistence::Value::of_entity(read_entity_ref(in));
        case PersistenceValueKind::Enum: {
            auto type    = read_string(in);
            auto variant = read_string(in);
            return persistence::Value::of_enum(std::move(type), std::move(variant));
        }
        case PersistenceValueKind::AssetRef:
            return persistence::Value::of_asset(read_string(in));
        case PersistenceValueKind::InputRef:
            return persistence::Value::of_input(read_string(in));
        case PersistenceValueKind::Struct: {
            persistence::StructValue value;
            const auto field_count = read_u32(in);
            value.fields.reserve(field_count);
            for (std::uint32_t i = 0; i < field_count; ++i) {
                value.fields.push_back(read_field(in));
            }
            return persistence::Value::of_struct(std::move(value));
        }
        case PersistenceValueKind::List: {
            persistence::ListValue value;
            const auto item_count = read_u32(in);
            value.items.reserve(item_count);
            for (std::uint32_t i = 0; i < item_count; ++i) {
                value.items.push_back(read_value(in));
            }
            return persistence::Value::of_list(std::move(value));
        }
        case PersistenceValueKind::Unsupported:
            return persistence::Value{};
    }
    throw std::runtime_error("persistence file adapter: unrecognized value kind byte");
}

void write_trait(std::ostream& out, const persistence::TraitRecord& trait) {
    write_string(out, trait.trait);
    write_u8(out, trait.present ? 1 : 0);
    write_u32(out, static_cast<std::uint32_t>(trait.construction.size()));
    for (const auto& field : trait.construction) {
        write_field(out, field);
    }
    write_u32(out, static_cast<std::uint32_t>(trait.persisted.size()));
    for (const auto& field : trait.persisted) {
        write_field(out, field);
    }
}

persistence::TraitRecord read_trait(std::istream& in) {
    persistence::TraitRecord trait;
    trait.trait   = read_string(in);
    trait.present = read_u8(in) != 0;
    const auto construction_count = read_u32(in);
    trait.construction.reserve(construction_count);
    for (std::uint32_t i = 0; i < construction_count; ++i) {
        trait.construction.push_back(read_field(in));
    }
    const auto persisted_count = read_u32(in);
    trait.persisted.reserve(persisted_count);
    for (std::uint32_t i = 0; i < persisted_count; ++i) {
        trait.persisted.push_back(read_field(in));
    }
    return trait;
}

void write_entity_record(std::ostream& out, const persistence::EntityRecord& record) {
    write_u32(out, record.id);
    write_string(out, record.archetype);
    write_entity_ref(out, record.parent);
    write_u32(out, static_cast<std::uint32_t>(record.traits.size()));
    for (const auto& trait : record.traits) {
        write_trait(out, trait);
    }
}

persistence::EntityRecord read_entity_record(std::istream& in) {
    persistence::EntityRecord record;
    record.id        = read_u32(in);
    record.archetype = read_string(in);
    record.parent    = read_entity_ref(in);
    const auto trait_count = read_u32(in);
    record.traits.reserve(trait_count);
    for (std::uint32_t i = 0; i < trait_count; ++i) {
        record.traits.push_back(read_trait(in));
    }
    return record;
}

void encode_snapshot(std::ostream& out, const persistence::Snapshot& snapshot) {
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    write_u32(out, kFileFormat);
    write_u32(out, snapshot.schema_revision);
    write_u64(out, snapshot.schema_fingerprint);
    write_string(out, snapshot.module);
    write_u32(out, static_cast<std::uint32_t>(snapshot.entities.size()));
    for (const auto& record : snapshot.entities) {
        write_entity_record(out, record);
    }
}

persistence::Snapshot decode_snapshot(std::istream& in) {
    std::array<char, kMagic.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != kMagic) {
        throw std::runtime_error("persistence file adapter: not a save file");
    }
    if (read_u32(in) != kFileFormat) {
        throw std::runtime_error("persistence file adapter: unsupported file format version");
    }

    persistence::Snapshot snapshot;
    snapshot.schema_revision    = read_u32(in);
    snapshot.schema_fingerprint = read_u64(in);
    snapshot.module             = read_string(in);
    const auto entity_count     = read_u32(in);
    snapshot.entities.reserve(entity_count);
    for (std::uint32_t i = 0; i < entity_count; ++i) {
        snapshot.entities.push_back(read_entity_record(in));
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
                            encode_snapshot(out, snapshot);
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
                    std::ifstream in(slot_file_path(directory, slot), std::ios::binary);
                    if (!in) {
                        return PersistenceReadResult{
                            .ok = false, .code = "io_failure", .message = "no save found for slot '" + slot + "'"};
                    }
                    in.exceptions(std::ios::failbit | std::ios::badbit);
                    return PersistenceReadResult{.ok = true, .snapshot = decode_snapshot(in)};
                } catch (const std::exception&) {
                    return PersistenceReadResult{
                        .ok = false, .code = "io_failure", .message = "failed to read slot '" + slot + "'"};
                }
            }};
}

}  // namespace cactus::runtime::entt_backend
