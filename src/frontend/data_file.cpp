#include "frontend/data_file.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>

namespace cactus {

// ── FieldValue helpers ───────────────────────────────────────────────────────

size_t FieldValue::byte_size() const {
    switch (tag) {
        case Tag::Int:
        case Tag::Float:
            return 4;
        case Tag::Bool:
            return 1;
        case Tag::Color:
            return 4;
        case Tag::Vec2:
            return 8;
        case Tag::Vec3:
            return 12;
        case Tag::Quat:
            return 16;
        case Tag::EntityId:
        case Tag::Enum:
            return 4;
        default:
            return 0;
    }
}

// ── DataFileWriter ───────────────────────────────────────────────────────────

DataFileWriter::DataFileWriter(const ProgramNode& ast, const DecoratedProgram& decorated, ErrorReporter& errors)
    : ast_(ast)
    , decorated_(decorated)
    , errors_(errors) {}

std::filesystem::path DataFileWriter::data_filename(const std::string& module_name) {
    return {module_name + "_data.bin"};
}

// ── Constant collection ──────────────────────────────────────────────────────

void DataFileWriter::collect_constants() {
    for (const auto& decl : ast_.declarations) {
        if (const auto* cb = std::get_if<ConstBlockNode>(&decl)) {
            for (const auto& assign : cb->assignments) {
                auto val = eval_expr(*assign.value);
                if (val) {
                    const_map_[assign.name] = *val;
                }
            }
        }
    }
}

void DataFileWriter::collect_enums() {
    for (const auto& decl : ast_.declarations) {
        if (const auto* en = std::get_if<EnumNode>(&decl)) {
            auto& variants = enum_map_[en->name];
            for (uint32_t i = 0; i < static_cast<uint32_t>(en->variants.size()); ++i) {
                variants[en->variants[i].name] = i;
            }
        }
    }
}

void DataFileWriter::build_trait_bit_index() {
    uint32_t bit = 0;
    // Assign bits in declaration order
    for (const auto& decl : ast_.declarations) {
        if (const auto* tr = std::get_if<TraitNode>(&decl)) {
            if (static_cast<unsigned int>(trait_bit_index_.contains(tr->name)) == 0U) {
                trait_bit_index_[tr->name] = bit++;
            }
        }
    }
    // Also cover imported traits from decorated program
    for (const auto& [name, _] : decorated_.traits) {
        if (static_cast<unsigned int>(trait_bit_index_.contains(name)) == 0U) {
            trait_bit_index_[name] = bit++;
        }
    }
}

uint64_t DataFileWriter::compute_trait_mask(const std::vector<ArchetypeTraitEntry>& traits) const {
    uint64_t mask = 0;
    for (const auto& entry : traits) {
        auto it = trait_bit_index_.find(entry.trait_name);
        if (it != trait_bit_index_.end() && it->second < 64) {
            mask |= (static_cast<uint64_t>(1) << it->second);
        }
    }
    return mask;
}

// ── Expression evaluator ─────────────────────────────────────────────────────

std::optional<FieldValue> DataFileWriter::eval_expr(const ExprNode& expr) const {
    return std::visit(
        [this](auto& e) -> std::optional<FieldValue> {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                return eval_literal(e);
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return eval_ident(e);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                return eval_call(e);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                return eval_member(e);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return eval_unary(e);
            } else {
                return std::nullopt;
            }
        },
        expr.expr);
}

static uint8_t hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(c - 'A' + 10);
    }
    return 0;
}

static uint8_t parse_hex_byte(const std::string& s, size_t offset) {
    return static_cast<uint8_t>((hex_digit(s[offset]) << 4) | hex_digit(s[offset + 1]));
}

std::optional<FieldValue> DataFileWriter::eval_literal(const LiteralExpr& lit) {
    FieldValue fv;
    switch (lit.kind) {
        case LiteralExpr::Kind::Int:
            fv.tag = FieldValue::Tag::Int;
            {
                const auto* begin    = lit.value.data();
                const auto* end      = begin + lit.value.size();
                int parsed           = 0;
                const auto [ptr, ec] = std::from_chars(begin, end, parsed);
                if (ec != std::errc{}) {
                    return std::nullopt;
                }
                fv.i32 = parsed;
            }
            return fv;
        case LiteralExpr::Kind::Float:
            fv.tag = FieldValue::Tag::Float;
            {
                const auto* begin    = lit.value.data();
                const auto* end      = begin + lit.value.size();
                float parsed         = 0.0F;
                const auto [ptr, ec] = std::from_chars(begin, end, parsed);
                if (ec != std::errc{}) {
                    return std::nullopt;
                }
                fv.f32 = parsed;
            }
            return fv;
        case LiteralExpr::Kind::Bool:
            fv.tag = FieldValue::Tag::Bool;
            fv.b   = (lit.value == "true");
            return fv;
        case LiteralExpr::Kind::HexColor: {
            // The lexer stores value WITHOUT the leading '#' — e.g. "FF8800" (6 chars)
            fv.tag        = FieldValue::Tag::Color;
            const auto& s = lit.value;
            if (s.size() >= 6) {
                fv.color.r  = parse_hex_byte(s, 0);
                fv.color.g  = parse_hex_byte(s, 2);
                fv.color.bv = parse_hex_byte(s, 4);
                fv.color.a  = (s.size() >= 8) ? parse_hex_byte(s, 6) : 255;
            }
            return fv;
        }
        case LiteralExpr::Kind::String:
        default:
            return std::nullopt;
    }
}

std::optional<FieldValue> DataFileWriter::eval_ident(const IdentExpr& ident) const {
    auto it = const_map_.find(ident.name);
    if (it != const_map_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<FieldValue> DataFileWriter::eval_call(const CallExpr& call) const {
    // Get function name
    auto* callee_ident = std::get_if<IdentExpr>(&call.callee->expr);
    if (callee_ident == nullptr) {
        return std::nullopt;
    }

    const auto& fname = callee_ident->name;

    // Evaluate all arguments
    std::vector<FieldValue> args;
    for (const auto& arg : call.args) {
        auto val = eval_expr(*arg);
        if (!val) {
            return std::nullopt;
        }
        args.push_back(*val);
    }

    auto get_float = [](const FieldValue& fv) -> std::optional<float> {
        if (fv.tag == FieldValue::Tag::Float) {
            return fv.f32;
        }
        if (fv.tag == FieldValue::Tag::Int) {
            return static_cast<float>(fv.i32);
        }
        return std::nullopt;
    };

    if (fname == "vec2" && args.size() == 2) {
        FieldValue fv;
        fv.tag = FieldValue::Tag::Vec2;
        auto x = get_float(args.at(0));
        auto y = get_float(args.at(1));
        if (!x || !y) {
            return std::nullopt;
        }
        fv.vec2.x = *x;
        fv.vec2.y = *y;
        return fv;
    }

    if (fname == "vec3" && args.size() == 3) {
        FieldValue fv;
        fv.tag = FieldValue::Tag::Vec3;
        auto x = get_float(args.at(0));
        auto y = get_float(args.at(1));
        auto z = get_float(args.at(2));
        if (!x || !y || !z) {
            return std::nullopt;
        }
        fv.vec3.x = *x;
        fv.vec3.y = *y;
        fv.vec3.z = *z;
        return fv;
    }

    if (fname == "quat" && args.size() == 4) {
        FieldValue fv;
        fv.tag = FieldValue::Tag::Quat;
        auto x = get_float(args[0]);
        auto y = get_float(args[1]);
        auto z = get_float(args[2]);
        auto w = get_float(args[3]);
        if (!x || !y || !z || !w) {
            return std::nullopt;
        }
        fv.quat.x = *x;
        fv.quat.y = *y;
        fv.quat.z = *z;
        fv.quat.w = *w;
        return fv;
    }

    return std::nullopt;
}

std::optional<FieldValue> DataFileWriter::eval_member(const MemberExpr& mem) const {
    // Handles enum member access: EnumType.Variant
    auto* obj_ident = std::get_if<IdentExpr>(&mem.object->expr);
    if (obj_ident == nullptr) {
        return std::nullopt;
    }

    auto enum_it = enum_map_.find(obj_ident->name);
    if (enum_it == enum_map_.end()) {
        return std::nullopt;
    }

    auto var_it = enum_it->second.find(mem.member);
    if (var_it == enum_it->second.end()) {
        return std::nullopt;
    }

    FieldValue fv;
    fv.tag      = FieldValue::Tag::Enum;
    fv.enum_idx = var_it->second;
    return fv;
}

std::optional<FieldValue> DataFileWriter::eval_unary(const UnaryExpr& un) const {
    auto val = eval_expr(*un.operand);
    if (!val) {
        return std::nullopt;
    }

    if (un.op == "-") {
        if (val->tag == FieldValue::Tag::Float) {
            val->f32 = -val->f32;
            return val;
        }
        if (val->tag == FieldValue::Tag::Int) {
            val->i32 = -val->i32;
            return val;
        }
    }
    if (un.op == "not") {
        if (val->tag == FieldValue::Tag::Bool) {
            val->b = !val->b;
            return val;
        }
    }
    return std::nullopt;
}

// ── Field value construction ─────────────────────────────────────────────────

static FieldValue default_for_type(TypeKind kind) {
    FieldValue fv;
    switch (kind) {
        case TypeKind::Int:
            fv.tag = FieldValue::Tag::Int;
            fv.i32 = 0;
            break;
        case TypeKind::Float:
            fv.tag = FieldValue::Tag::Float;
            fv.f32 = 0.0F;
            break;
        case TypeKind::Bool:
            fv.tag = FieldValue::Tag::Bool;
            fv.b   = false;
            break;
        case TypeKind::Color:
            fv.tag = FieldValue::Tag::Color;
            break;
        case TypeKind::Vec2:
            fv.tag = FieldValue::Tag::Vec2;
            break;
        case TypeKind::Vec3:
            fv.tag = FieldValue::Tag::Vec3;
            break;
        case TypeKind::Quat:
            fv.tag = FieldValue::Tag::Quat;
            break;
        case TypeKind::EntityId:
            fv.tag       = FieldValue::Tag::EntityId;
            fv.entity_id = 0;
            break;
        case TypeKind::Enum:
            fv.tag      = FieldValue::Tag::Enum;
            fv.enum_idx = 0;
            break;
        case TypeKind::List:
            fv.tag = FieldValue::Tag::List;
            break;
        case TypeKind::Struct:
            fv.tag = FieldValue::Tag::Struct;
            break;
        default:
            break;
    }
    return fv;
}

FieldValue DataFileWriter::make_field_value(const ResolvedField& field, const std::optional<FieldValue>& config_val) {
    if (config_val && config_val->is_serializable()) {
        return *config_val;
    }
    // Use type-appropriate default
    return default_for_type(field.type.kind);
}

// ── Build entity records ─────────────────────────────────────────────────────

std::vector<EntityInstanceData> DataFileWriter::build_records() {  // NOLINT(readability-function-cognitive-complexity)
    // Prepare evaluation tables
    collect_constants();
    collect_enums();
    build_trait_bit_index();

    std::vector<EntityInstanceData> records;

    for (const auto& decl : ast_.declarations) {
        if (const auto* entity = std::get_if<EntityNode>(&decl)) {
            EntityInstanceData rec;
            rec.name       = entity->name;
            rec.trait_mask = compute_trait_mask(entity->traits);

            // Build config map from nested trait assignments: field_name → evaluated value
            std::unordered_map<std::string, FieldValue> config_vals;
            for (const auto& trait : entity->traits) {
                for (const auto& assign : trait.assignments) {
                    auto val = eval_expr(*assign.value);
                    if (val) {
                        config_vals[assign.name] = *val;
                    }
                }
            }

            // For each declared trait entry, emit all fields in declaration order
            for (const auto& entry : entity->traits) {
                auto it = decorated_.traits.find(entry.trait_name);
                if (it == decorated_.traits.end()) {
                    continue;
                }

                for (const auto& field : it->second.fields) {
                    auto cfg_it = config_vals.find(field.name);
                    std::optional<FieldValue> cfg_val;
                    if (cfg_it != config_vals.end()) {
                        cfg_val = cfg_it->second;
                    }

                    FieldValue fv = make_field_value(field, cfg_val);
                    if (fv.is_serializable()) {
                        rec.fields.emplace_back(field.name, fv);
                    }
                }
            }

            records.push_back(std::move(rec));
        }
        // TemplateNode → skip (templates produce no data file entries on their own)
    }

    return records;
}

// ── Write binary helpers ─────────────────────────────────────────────────────

void DataFileWriter::write_u8(std::ostream& out, uint8_t v) {
    out.put(static_cast<char>(v));
}

void DataFileWriter::write_u16(std::ostream& out, uint16_t v) {
    out.put(static_cast<char>(v & 0xFFU));
    out.put(static_cast<char>((v >> 8U) & 0xFFU));
}

void DataFileWriter::write_u32(std::ostream& out, uint32_t v) {
    out.put(static_cast<char>(v & 0xFFU));
    out.put(static_cast<char>((v >> 8U) & 0xFFU));
    out.put(static_cast<char>((v >> 16U) & 0xFFU));
    out.put(static_cast<char>((v >> 24U) & 0xFFU));
}

void DataFileWriter::write_u64(std::ostream& out, uint64_t v) {
    for (unsigned int i = 0; i < 8U; ++i) {
        out.put(static_cast<char>((v >> (i * 8U)) & 0xFFU));
    }
}

void DataFileWriter::write_str_short(std::ostream& out, const std::string& s) {
    write_u16(out, static_cast<uint16_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

void DataFileWriter::write_field_value(std::ostream& out, const FieldValue& fv) {
    write_u8(out, static_cast<uint8_t>(fv.tag));
    switch (fv.tag) {
        case FieldValue::Tag::Int: {
            uint32_t v = 0;
            std::memcpy(&v, &fv.i32, 4);
            write_u32(out, v);
            break;
        }
        case FieldValue::Tag::Float: {
            uint32_t v = 0;
            std::memcpy(&v, &fv.f32, 4);
            write_u32(out, v);
            break;
        }
        case FieldValue::Tag::Bool:
            write_u8(out, fv.b ? 1 : 0);
            break;
        case FieldValue::Tag::Color:
            write_u8(out, fv.color.r);
            write_u8(out, fv.color.g);
            write_u8(out, fv.color.bv);
            write_u8(out, fv.color.a);
            break;
        case FieldValue::Tag::Vec2: {
            uint32_t x = 0;
            uint32_t y = 0;
            std::memcpy(&x, &fv.vec2.x, 4);
            std::memcpy(&y, &fv.vec2.y, 4);
            write_u32(out, x);
            write_u32(out, y);
            break;
        }
        case FieldValue::Tag::Vec3: {
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t z = 0;
            std::memcpy(&x, &fv.vec3.x, 4);
            std::memcpy(&y, &fv.vec3.y, 4);
            std::memcpy(&z, &fv.vec3.z, 4);
            write_u32(out, x);
            write_u32(out, y);
            write_u32(out, z);
            break;
        }
        case FieldValue::Tag::Quat: {
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t z = 0;
            uint32_t w = 0;
            std::memcpy(&x, &fv.quat.x, 4);
            std::memcpy(&y, &fv.quat.y, 4);
            std::memcpy(&z, &fv.quat.z, 4);
            std::memcpy(&w, &fv.quat.w, 4);
            write_u32(out, x);
            write_u32(out, y);
            write_u32(out, z);
            write_u32(out, w);
            break;
        }
        case FieldValue::Tag::EntityId:
            write_u32(out, fv.entity_id);
            break;
        case FieldValue::Tag::Enum:
            write_u32(out, fv.enum_idx);
            break;
        default:
            break;
    }
}

// ── Write to disk ────────────────────────────────────────────────────────────

std::vector<EntityInstanceData> DataFileWriter::write(const std::filesystem::path& build_dir,
                                                      const std::string& module_name) {
    auto records = build_records();

    std::filesystem::create_directories(build_dir);
    auto out_path = build_dir / data_filename(module_name);
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        errors_.error({}, "failed to open data file for writing: " + out_path.string());
        return records;
    }

    // Magic
    out.write(MAGIC, 4);
    // Version
    write_u16(out, CURRENT_VERSION);
    // Entity count
    write_u32(out, static_cast<uint32_t>(records.size()));

    // Entity records
    for (auto& rec : records) {
        write_str_short(out, rec.name);
        write_u32(out, static_cast<uint32_t>(rec.fields.size()));
        for (auto& [fname, fval] : rec.fields) {
            write_str_short(out, fname);
            write_field_value(out, fval);
        }
        write_u64(out, rec.trait_mask);
    }

    return records;
}

// ── DataFileReader ───────────────────────────────────────────────────────────

DataFileReader::DataFileReader(ErrorReporter& errors)
    : errors_(errors) {}

uint8_t DataFileReader::read_u8(std::istream& in) {
    return static_cast<uint8_t>(in.get());
}

uint16_t DataFileReader::read_u16(std::istream& in) {
    const uint32_t LO = static_cast<uint8_t>(in.get());
    const uint32_t HI = static_cast<uint8_t>(in.get());
    return static_cast<uint16_t>(LO | (HI << 8U));
}

uint32_t DataFileReader::read_u32(std::istream& in) {
    const uint32_t B0 = static_cast<uint8_t>(in.get());
    const uint32_t B1 = static_cast<uint8_t>(in.get());
    const uint32_t B2 = static_cast<uint8_t>(in.get());
    const uint32_t B3 = static_cast<uint8_t>(in.get());
    return B0 | (B1 << 8U) | (B2 << 16U) | (B3 << 24U);
}

uint64_t DataFileReader::read_u64(std::istream& in) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(read_u8(in)) << (i * 8);
    }
    return v;
}

std::string DataFileReader::read_str_short(std::istream& in) {
    uint16_t len = read_u16(in);
    std::string s(len, '\0');
    in.read(s.data(), len);
    return s;
}

FieldValue DataFileReader::read_field_value(std::istream& in, FieldValue::Tag tag) {
    FieldValue fv;
    fv.tag = tag;
    switch (tag) {
        case FieldValue::Tag::Int: {
            uint32_t v = read_u32(in);
            std::memcpy(&fv.i32, &v, 4);
            break;
        }
        case FieldValue::Tag::Float: {
            uint32_t v = read_u32(in);
            std::memcpy(&fv.f32, &v, 4);
            break;
        }
        case FieldValue::Tag::Bool:
            fv.b = (read_u8(in) != 0);
            break;
        case FieldValue::Tag::Color:
            fv.color.r  = read_u8(in);
            fv.color.g  = read_u8(in);
            fv.color.bv = read_u8(in);
            fv.color.a  = read_u8(in);
            break;
        case FieldValue::Tag::Vec2: {
            uint32_t x = read_u32(in);
            uint32_t y = read_u32(in);
            std::memcpy(&fv.vec2.x, &x, 4);
            std::memcpy(&fv.vec2.y, &y, 4);
            break;
        }
        case FieldValue::Tag::Vec3: {
            uint32_t x = read_u32(in);
            uint32_t y = read_u32(in);
            uint32_t z = read_u32(in);
            std::memcpy(&fv.vec3.x, &x, 4);
            std::memcpy(&fv.vec3.y, &y, 4);
            std::memcpy(&fv.vec3.z, &z, 4);
            break;
        }
        case FieldValue::Tag::Quat: {
            uint32_t x = read_u32(in);
            uint32_t y = read_u32(in);
            uint32_t z = read_u32(in);
            uint32_t w = read_u32(in);
            std::memcpy(&fv.quat.x, &x, 4);
            std::memcpy(&fv.quat.y, &y, 4);
            std::memcpy(&fv.quat.z, &z, 4);
            std::memcpy(&fv.quat.w, &w, 4);
            break;
        }
        case FieldValue::Tag::EntityId:
            fv.entity_id = read_u32(in);
            break;
        case FieldValue::Tag::Enum:
            fv.enum_idx = read_u32(in);
            break;
        default:
            fv.tag = FieldValue::Tag::Unknown;
            break;
    }
    return fv;
}

std::vector<EntityInstanceData> DataFileReader::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors_.error({}, "cannot open data file: " + path.string());
        return {};
    }

    // Read and verify magic
    std::array<char, 4> magic = {};
    in.read(magic.data(), magic.size());
    if (!in || std::strncmp(magic.data(), DataFileWriter::MAGIC, magic.size()) != 0) {
        errors_.error({}, "invalid data file magic in: " + path.string());
        return {};
    }

    // Read and check version (task 6.6)
    uint16_t version = read_u16(in);
    if (version != DataFileWriter::CURRENT_VERSION) {
        errors_.error({},
                      "data file version mismatch: expected " + std::to_string(DataFileWriter::CURRENT_VERSION) +
                          ", got " + std::to_string(version) + " in: " + path.string());
        return {};
    }

    uint32_t entity_count = read_u32(in);
    std::vector<EntityInstanceData> records;
    records.reserve(entity_count);

    for (uint32_t i = 0; i < entity_count; ++i) {
        EntityInstanceData rec;
        rec.name             = read_str_short(in);
        uint32_t field_count = read_u32(in);
        rec.fields.reserve(field_count);

        for (uint32_t j = 0; j < field_count; ++j) {
            std::string fname = read_str_short(in);
            auto tag          = static_cast<FieldValue::Tag>(read_u8(in));
            FieldValue fv     = read_field_value(in, tag);
            rec.fields.emplace_back(fname, fv);
        }

        rec.trait_mask = read_u64(in);
        records.push_back(std::move(rec));
    }

    return records;
}

}  // namespace cactus
