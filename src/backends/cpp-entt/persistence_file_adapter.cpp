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
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

namespace {

// ── Versioned example-only binary encoding ──────────────────────────────────
// A magic/version header, the document's tables, then its rows. Counts,
// indexes and integers are LEB128 varints (signed ones zigzagged), floats keep
// their bits, and a vector stores only its lanes. Each present trait row starts
// with one bit per field: a set bit means the field repeats the bytes it had
// in the previous row of the same layout, and is not written again. The format
// carries no compatibility promise across compiler versions.

constexpr std::array<char, 4> kMagic = {'C', 'P', 'S', 'T'};
constexpr std::uint32_t kFileFormat  = 4;

class IncompatibleFormat : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

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

    void index(std::uint32_t value) { varint(value == persistence::kNoIndex ? 0 : std::uint64_t{value} + 1); }

    void f32(float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(bits >> shift));
        }
    }

    void text(std::string_view value) {
        varint(value.size());
        bytes_.append(value);
    }

private:
    std::string bytes_;
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

    // A count of things each taking at least one byte, so never more than remain.
    std::uint32_t count() {
        const auto value = varint();
        if (value > bytes_.size() - offset_) {
            throw std::runtime_error("persistence file adapter: count exceeds the save size");
        }
        return static_cast<std::uint32_t>(value);
    }

    std::uint32_t index() {
        const auto value = varint();
        if (value > 0xFFFFFFFFULL) {
            throw std::runtime_error("persistence file adapter: index out of range");
        }
        return value == 0 ? persistence::kNoIndex : static_cast<std::uint32_t>(value - 1);
    }

    float f32() {
        std::uint32_t bits = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            bits |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return std::bit_cast<float>(bits);
    }

    std::string text() {
        const auto size = count();
        std::string value(bytes_.substr(offset_, size));
        offset_ += size;
        return value;
    }

    void expect(std::string_view text) {
        for (const char expected : text) {
            if (static_cast<char>(u8()) != expected) {
                throw std::runtime_error("persistence file adapter: not a save file");
            }
        }
    }

    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t offset_ = 0;
};

[[nodiscard]] const persistence::DocumentType& type_at(const persistence::Snapshot& document, std::uint32_t index) {
    const auto* type = document.type(index);
    if (type == nullptr) {
        throw std::runtime_error("persistence file adapter: unknown value type");
    }
    return *type;
}

[[nodiscard]] const std::vector<persistence::DocumentField>& struct_at(const persistence::Snapshot& document,
                                                                       std::uint32_t index) {
    if (index >= document.struct_shapes().size()) {
        throw std::runtime_error("persistence file adapter: unknown struct layout");
    }
    return document.struct_shapes()[index];
}

void check_depth(std::size_t depth) {
    // A crafted file could otherwise nest deep enough to overflow the stack.
    if (depth > persistence::kMaxPersistenceNestingDepth) {
        throw std::runtime_error("persistence file adapter: value nested too deeply");
    }
}

// Re-encodes one value from the document's fixed-width bytes into the file.
void write_value(Encoder& out, const persistence::Snapshot& document, std::uint32_t type_index,
                 persistence::ValueReader& in, std::size_t depth) {
    check_depth(depth);
    const auto& type = type_at(document, type_index);
    switch (type.kind) {
        case PersistenceValueKind::Bool:
            out.u8(in.get_bool() ? 1 : 0);
            return;
        case PersistenceValueKind::Int:
            out.signed_varint(in.get_int());
            return;
        case PersistenceValueKind::Float:
            out.f32(in.get_float());
            return;
        case PersistenceValueKind::Vector:
            for (std::uint8_t lane = 0; lane < type.lanes; ++lane) {
                out.f32(in.get_float());
            }
            return;
        case PersistenceValueKind::EntityRef: {
            const auto ref = in.get_entity();
            out.u8(ref.present ? 1 : 0);
            out.varint(ref.id);
            return;
        }
        case PersistenceValueKind::String:
        case PersistenceValueKind::AssetRef:
        case PersistenceValueKind::InputRef:
        case PersistenceValueKind::Enum:
            out.varint(in.get_index());
            return;
        case PersistenceValueKind::Struct:
            for (const auto& field : struct_at(document, type.ref)) {
                write_value(out, document, field.type, in, depth + 1);
            }
            return;
        case PersistenceValueKind::List: {
            const auto count = in.get_index();
            out.varint(count);
            for (std::uint32_t item = 0; item < count && !in.failed(); ++item) {
                const auto item_type = type.ref == persistence::kNoIndex ? in.get_index() : type.ref;
                if (type.ref == persistence::kNoIndex) {
                    out.varint(item_type);
                }
                write_value(out, document, item_type, in, depth + 1);
            }
            return;
        }
        case PersistenceValueKind::Unsupported:
            return;
    }
}

// The inverse of write_value: one value from the file into the builder.
void read_value(Decoder& in, persistence::SnapshotBuilder& out, std::uint32_t type_index, std::size_t depth) {
    check_depth(depth);
    const auto type = type_at(out.document(), type_index);
    switch (type.kind) {
        case PersistenceValueKind::Bool:
            out.put_bool(in.u8() != 0);
            return;
        case PersistenceValueKind::Int:
            out.put_int(static_cast<std::int32_t>(in.signed_varint()));
            return;
        case PersistenceValueKind::Float:
            out.put_float(in.f32());
            return;
        case PersistenceValueKind::Vector:
            for (std::uint8_t lane = 0; lane < type.lanes; ++lane) {
                out.put_float(in.f32());
            }
            return;
        case PersistenceValueKind::EntityRef: {
            const bool present = in.u8() != 0;
            out.put_entity({.id = static_cast<persistence::DocumentId>(in.varint()), .present = present});
            return;
        }
        case PersistenceValueKind::String:
        case PersistenceValueKind::AssetRef:
        case PersistenceValueKind::InputRef:
        case PersistenceValueKind::Enum:
            out.put_index(static_cast<std::uint32_t>(in.varint()));
            return;
        case PersistenceValueKind::Struct: {
            // Tables stay put while rows are read, so the reference stays valid.
            const auto& fields = struct_at(out.document(), type.ref);
            for (const auto& field : fields) {
                read_value(in, out, field.type, depth + 1);
            }
            return;
        }
        case PersistenceValueKind::List: {
            const auto count = in.count();
            out.put_index(count);
            for (std::uint32_t item = 0; item < count; ++item) {
                auto item_type = type.ref;
                if (item_type == persistence::kNoIndex) {
                    item_type = static_cast<std::uint32_t>(in.varint());
                    out.put_index(item_type);
                }
                read_value(in, out, item_type, depth + 1);
            }
            return;
        }
        case PersistenceValueKind::Unsupported:
            return;
    }
    throw std::runtime_error("persistence file adapter: unrecognized value kind");
}

void write_fields(Encoder& out, const std::vector<persistence::DocumentField>& fields) {
    out.varint(fields.size());
    for (const auto& field : fields) {
        out.varint(field.name);
        out.varint(field.type);
    }
}

std::vector<persistence::DocumentField> read_fields(Decoder& in) {
    std::vector<persistence::DocumentField> fields(in.count());
    for (auto& field : fields) {
        field.name = static_cast<std::uint32_t>(in.varint());
        field.type = static_cast<std::uint32_t>(in.varint());
    }
    return fields;
}

void write_tables(Encoder& out, const persistence::Snapshot& document) {
    out.varint(document.names().size());
    for (const auto& name : document.names()) {
        out.text(name);
    }
    out.varint(document.types().size());
    for (const auto& type : document.types()) {
        out.u8(static_cast<std::uint8_t>(type.kind));
        out.u8(type.lanes);
        out.u8(static_cast<std::uint8_t>(type.lane_kind));
        out.index(type.ref);
    }
    out.varint(document.struct_shapes().size());
    for (const auto& fields : document.struct_shapes()) {
        write_fields(out, fields);
    }
    out.varint(document.enums().size());
    for (const auto& entry : document.enums()) {
        out.varint(entry.name);
        out.varint(entry.variants.size());
        for (const auto variant : entry.variants) {
            out.varint(variant);
        }
    }
    out.varint(document.trait_shapes().size());
    for (const auto& shape : document.trait_shapes()) {
        out.varint(shape.name);
        write_fields(out, shape.construction);
        write_fields(out, shape.persisted);
    }
}

persistence::SnapshotBuilder read_tables(Decoder& in) {
    persistence::DocumentTables tables;
    tables.names.resize(in.count());
    for (auto& name : tables.names) {
        name = in.text();
    }
    tables.types.resize(in.count());
    for (auto& type : tables.types) {
        type.kind      = static_cast<PersistenceValueKind>(in.u8());
        type.lanes     = in.u8();
        type.lane_kind = static_cast<PersistenceValueKind>(in.u8());
        type.ref       = in.index();
        if (type.kind > PersistenceValueKind::Unsupported || type.lanes > 4) {
            throw std::runtime_error("persistence file adapter: malformed value type");
        }
    }
    tables.structs.resize(in.count());
    for (auto& fields : tables.structs) {
        fields = read_fields(in);
    }
    tables.enums.resize(in.count());
    for (auto& entry : tables.enums) {
        entry.name = static_cast<std::uint32_t>(in.varint());
        entry.variants.resize(in.count());
        for (auto& variant : entry.variants) {
            variant = static_cast<std::uint32_t>(in.varint());
        }
    }
    tables.traits.resize(in.count());
    for (auto& shape : tables.traits) {
        shape.name         = static_cast<std::uint32_t>(in.varint());
        shape.construction = read_fields(in);
        shape.persisted    = read_fields(in);
    }
    return persistence::SnapshotBuilder::with_tables(std::move(tables));
}

// Where one field of a layout's previous row sits in the arena.
struct FieldSpan {
    std::size_t begin = 0;
    std::size_t size  = 0;
};

// Per layout: its field types in value order and the previous row's field
// spans, which the repeat bits compare against. Scratch space is reused, so a
// row costs no allocation.
class RowLayouts {
public:
    explicit RowLayouts(const persistence::Snapshot& document) {
        layouts_.reserve(document.trait_shapes().size());
        for (const auto& shape : document.trait_shapes()) {
            Layout layout{.split = shape.construction.size()};
            for (const auto* bucket : {&shape.construction, &shape.persisted}) {
                for (const auto& field : *bucket) {
                    layout.types.push_back(field.type);
                }
            }
            layouts_.push_back(std::move(layout));
        }
    }

    struct Layout {
        std::vector<std::uint32_t> types;
        std::size_t split = 0;
        std::vector<FieldSpan> last;  // empty until a row of this layout was seen
    };

    [[nodiscard]] Layout& at(std::uint32_t shape) {
        if (shape >= layouts_.size()) {
            throw std::runtime_error("persistence file adapter: trait row names no layout");
        }
        return layouts_[shape];
    }

    std::vector<FieldSpan>& spans(std::size_t count) {
        spans_.assign(count, FieldSpan{});
        return spans_;
    }

    std::vector<std::uint8_t>& mask(std::size_t fields) {
        mask_.assign((fields + 7) / 8, 0);
        return mask_;
    }

    // The spans just filled become the layout's previous row.
    static void remember(Layout& layout, std::vector<FieldSpan>& spans) { layout.last.swap(spans); }

private:
    std::vector<Layout> layouts_;
    std::vector<FieldSpan> spans_;
    std::vector<std::uint8_t> mask_;
};

[[nodiscard]] bool repeats(std::span<const std::uint8_t> mask, std::size_t field) {
    return (mask[field / 8] & (1U << (field % 8))) != 0;
}

void write_trait_row(Encoder& out, const persistence::Snapshot& document, const persistence::TraitView& trait,
                     RowLayouts& layouts) {
    const auto& row = trait.row();
    out.varint(row.shape);
    out.u8(row.present ? 1 : 0);
    if (!row.present) {
        return;
    }
    auto& layout      = layouts.at(row.shape);
    const auto& types = layout.types;
    const auto arena  = document.arena();
    auto& spans       = layouts.spans(types.size());
    auto in           = trait.reader();
    for (std::size_t index = 0; index < types.size(); ++index) {
        const auto begin = in.offset();
        document.skip_value(types[index], in);
        spans[index] = FieldSpan{.begin = static_cast<std::size_t>(row.begin) + begin, .size = in.offset() - begin};
    }
    if (in.failed()) {
        throw std::runtime_error("persistence file adapter: malformed value bytes");
    }
    auto& mask = layouts.mask(types.size());
    for (std::size_t index = 0; index < types.size() && !layout.last.empty(); ++index) {
        const auto now    = arena.subspan(spans[index].begin, spans[index].size);
        const auto before = arena.subspan(layout.last[index].begin, layout.last[index].size);
        if (std::ranges::equal(now, before)) {
            mask[index / 8] = static_cast<std::uint8_t>(mask[index / 8] | (1U << (index % 8)));
        }
    }
    for (const auto byte : mask) {
        out.u8(byte);
    }
    for (std::size_t index = 0; index < types.size(); ++index) {
        if (!repeats(mask, index)) {
            persistence::ValueReader value(document, arena.subspan(spans[index].begin, spans[index].size));
            write_value(out, document, types[index], value, 0);
        }
    }
    RowLayouts::remember(layout, spans);
}

void read_trait_row(Decoder& in, persistence::SnapshotBuilder& out, RowLayouts& layouts) {
    const auto shape   = static_cast<std::uint32_t>(in.varint());
    const bool present = in.u8() != 0;
    auto& layout       = layouts.at(shape);
    out.begin_trait(shape, present);
    if (!present) {
        out.end_trait();
        return;
    }
    const auto& types = layout.types;
    auto& mask        = layouts.mask(types.size());
    for (auto& byte : mask) {
        byte = in.u8();
    }
    auto& spans = layouts.spans(types.size());
    for (std::size_t index = 0; index < types.size(); ++index) {
        if (index == layout.split) {
            out.begin_persisted();
        }
        const auto begin = out.document().arena().size();
        if (!repeats(mask, index)) {
            read_value(in, out, types[index], 0);
        } else if (layout.last.empty()) {
            throw std::runtime_error("persistence file adapter: repeat without a previous value");
        } else {
            out.put_repeat(layout.last[index].begin, layout.last[index].size);
        }
        spans[index] = FieldSpan{.begin = begin, .size = out.document().arena().size() - begin};
    }
    if (layout.split == types.size()) {
        out.begin_persisted();
    }
    out.end_trait();
    RowLayouts::remember(layout, spans);
}

// Relation rows follow the entities: layout, both ends, then the field values.
void write_relation_rows(Encoder& out, const persistence::Snapshot& document) {
    out.varint(document.relation_rows().size());
    for (const auto& row : document.relation_rows()) {
        const auto shape = row.values.shape;
        if (shape >= document.trait_shapes().size()) {
            throw std::runtime_error("persistence file adapter: relation row names no layout");
        }
        out.varint(shape);
        out.varint(row.from);
        out.varint(row.to);
        persistence::ValueReader in(document, document.payload(row.values, false));
        for (const auto& field : document.trait_shapes()[shape].construction) {
            write_value(out, document, field.type, in, 0);
        }
        if (in.failed()) {
            throw std::runtime_error("persistence file adapter: malformed value bytes");
        }
    }
}

void read_relation_rows(Decoder& in, persistence::SnapshotBuilder& out) {
    const auto rows = in.count();
    for (std::uint32_t index = 0; index < rows; ++index) {
        const auto shape = static_cast<std::uint32_t>(in.varint());
        const auto from  = static_cast<persistence::DocumentId>(in.varint());
        const auto to    = static_cast<persistence::DocumentId>(in.varint());
        if (shape >= out.document().trait_shapes().size()) {
            throw std::runtime_error("persistence file adapter: relation row names no layout");
        }
        // Copied: reading values may add names, which never touches layouts, but keep it independent.
        const auto fields = out.document().trait_shapes()[shape].construction;
        out.begin_relation_row(shape, from, to);
        for (const auto& field : fields) {
            read_value(in, out, field.type, 0);
        }
        out.end_relation_row();
    }
}

std::string encode_snapshot(const persistence::Snapshot& snapshot) {
    Encoder out;
    out.raw(std::string_view(kMagic.data(), kMagic.size()));
    out.varint(kFileFormat);
    out.varint(snapshot.schema_revision());
    out.varint(snapshot.schema_fingerprint());
    out.text(snapshot.module());
    out.u8(snapshot.truncated() ? 1 : 0);
    write_tables(out, snapshot);
    RowLayouts layouts(snapshot);
    out.varint(snapshot.entity_count());
    persistence::DocumentId last_id = 0;
    for (const auto entity : snapshot.entities()) {
        out.signed_varint(static_cast<std::int64_t>(entity.id()) - static_cast<std::int64_t>(last_id));
        last_id = entity.id();
        out.varint(entity.row().archetype);
        out.u8(entity.parent().present ? 1 : 0);
        out.varint(entity.parent().id);
        out.varint(entity.trait_count());
        for (std::size_t index = 0; index < entity.trait_count(); ++index) {
            write_trait_row(out, snapshot, entity.trait_at(index), layouts);
        }
    }
    write_relation_rows(out, snapshot);
    return out.take();
}

persistence::Snapshot decode_snapshot(std::string_view bytes) {
    Decoder in(bytes);
    in.expect(std::string_view(kMagic.data(), kMagic.size()));
    if (const auto format = in.varint(); format != kFileFormat) {
        throw IncompatibleFormat("save file format " + std::to_string(format) + " is not readable by this build");
    }
    const auto revision    = static_cast<std::uint32_t>(in.varint());
    const auto fingerprint = in.varint();
    auto module            = in.text();
    const bool truncated   = in.u8() != 0;
    auto out               = read_tables(in);
    out.set_schema(revision, fingerprint);
    out.set_module(std::move(module));
    if (truncated) {
        out.mark_truncated();
    }
    RowLayouts layouts(out.document());
    const auto entities = in.count();
    out.reserve(entities, entities, 0);
    std::int64_t id = 0;
    for (std::uint32_t entity = 0; entity < entities; ++entity) {
        id += in.signed_varint();
        const auto archetype = static_cast<std::uint32_t>(in.varint());
        const bool present   = in.u8() != 0;
        const auto parent    = static_cast<persistence::DocumentId>(in.varint());
        out.begin_entity(static_cast<persistence::DocumentId>(id), archetype, {.id = parent, .present = present});
        const auto traits = in.count();
        for (std::uint32_t trait = 0; trait < traits; ++trait) {
            read_trait_row(in, out, layouts);
        }
    }
    read_relation_rows(in, out);
    if (!in.at_end()) {
        throw std::runtime_error("persistence file adapter: trailing bytes after the document");
    }
    return std::move(out).finish();
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
                } catch (const IncompatibleFormat& error) {
                    return PersistenceReadResult{.ok = false, .code = "incompatible_schema", .message = error.what()};
                } catch (const std::exception&) {
                    return PersistenceReadResult{
                        .ok = false, .code = "io_failure", .message = "failed to read slot '" + slot + "'"};
                }
            }};
}

}  // namespace cactus::runtime::entt_backend
