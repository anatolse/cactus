#include "frontend/module_artifact.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace cactus {

namespace fs = std::filesystem;

ModuleArtifact::ModuleArtifact(ErrorReporter& errors) : errors_(errors) {}

// ── Artifact filename ───────────────────────────────────────────────────────

fs::path ModuleArtifact::artifact_filename(const std::string& module_name) {
    return fs::path(module_name + ".cmod");
}

// ── Write helpers ───────────────────────────────────────────────────────────

void ModuleArtifact::write_u8(std::ostream& out, uint8_t v) {
    out.write(reinterpret_cast<const char*>(&v), 1);
}

void ModuleArtifact::write_u32(std::ostream& out, uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), 4);
}

void ModuleArtifact::write_bool(std::ostream& out, bool v) {
    write_u8(out, v ? 1 : 0);
}

void ModuleArtifact::write_str(std::ostream& out, const std::string& s) {
    write_u32(out, static_cast<uint32_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

void ModuleArtifact::write_type_info(std::ostream& out, const TypeInfo& t) {
    write_u8(out, static_cast<uint8_t>(t.kind));
    write_str(out, t.name);
    write_bool(out, t.is_let);
    write_bool(out, t.is_persist);
    write_bool(out, t.is_sync);
    write_bool(out, t.is_pub);

    // For List: write element type
    bool has_element = (t.kind == TypeKind::List && t.element);
    write_bool(out, has_element);
    if (has_element) {
        write_type_info(out, *t.element);
    }

    // For Func: write params + return type
    write_u32(out, static_cast<uint32_t>(t.params.size()));
    for (auto& p : t.params) {
        write_type_info(out, p);
    }
    bool has_ret = (t.kind == TypeKind::Func && t.ret);
    write_bool(out, has_ret);
    if (has_ret) {
        write_type_info(out, *t.ret);
    }
}

void ModuleArtifact::write_traits(
    std::ostream& out,
    const std::unordered_map<std::string, ResolvedTrait>& traits) {
    write_u32(out, static_cast<uint32_t>(traits.size()));
    for (auto& [name, trait] : traits) {
        write_str(out, trait.name);
        write_bool(out, trait.is_pub);
        write_u32(out, static_cast<uint32_t>(trait.fields.size()));
        for (auto& field : trait.fields) {
            write_str(out, field.name);
            write_bool(out, field.is_let);
            write_bool(out, field.is_var);
            write_bool(out, field.is_persist);
            write_bool(out, field.is_sync);
            write_bool(out, field.is_pub);
            write_type_info(out, field.type);
        }
    }
}

void ModuleArtifact::write_structs(
    std::ostream& out,
    const std::unordered_map<std::string, ResolvedStruct>& structs) {
    write_u32(out, static_cast<uint32_t>(structs.size()));
    for (auto& [name, strct] : structs) {
        write_str(out, strct.name);
        write_u32(out, static_cast<uint32_t>(strct.fields.size()));
        for (auto& field : strct.fields) {
            write_str(out, field.name);
            write_bool(out, field.is_let);
            write_bool(out, field.is_var);
            write_bool(out, field.is_persist);
            write_bool(out, field.is_sync);
            write_bool(out, field.is_pub);
            write_type_info(out, field.type);
        }
    }
}

void ModuleArtifact::write_enums(
    std::ostream& out,
    const std::unordered_map<std::string, ResolvedEnum>& enums) {
    write_u32(out, static_cast<uint32_t>(enums.size()));
    for (auto& [name, enm] : enums) {
        write_str(out, enm.name);
        write_u32(out, static_cast<uint32_t>(enm.variants.size()));
        for (auto& v : enm.variants) {
            write_str(out, v);
        }
    }
}

void ModuleArtifact::write_dep_graph(std::ostream& out,
                                      const std::vector<SystemDependency>& graph) {
    write_u32(out, static_cast<uint32_t>(graph.size()));
    for (auto& dep : graph) {
        write_str(out, dep.system_name);
        // reads
        write_u32(out, static_cast<uint32_t>(dep.reads.size()));
        for (auto& r : dep.reads) write_str(out, r);
        // writes
        write_u32(out, static_cast<uint32_t>(dep.writes.size()));
        for (auto& w : dep.writes) write_str(out, w);
        // emits
        write_u32(out, static_cast<uint32_t>(dep.emits.size()));
        for (auto& e : dep.emits) write_str(out, e);
    }
}

void ModuleArtifact::write_string_pool(std::ostream& out, const StringPool& pool) {
    // We can't directly iterate over private maps, so we use size=0 as a sentinel.
    // In practice the string pool is used internally; for artifact purposes we
    // serialize a count of 0 and rebuild on load.
    write_u32(out, 0);
    // Future: serialize pool entries if direct access is added.
    (void)pool;
}

// ── Read helpers ─────────────────────────────────────────────────────────────

uint8_t ModuleArtifact::read_u8(std::istream& in) {
    uint8_t v = 0;
    in.read(reinterpret_cast<char*>(&v), 1);
    return v;
}

uint32_t ModuleArtifact::read_u32(std::istream& in) {
    uint32_t v = 0;
    in.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

bool ModuleArtifact::read_bool(std::istream& in) {
    return read_u8(in) != 0;
}

std::string ModuleArtifact::read_str(std::istream& in) {
    uint32_t len = read_u32(in);
    if (len > 1024 * 1024) {
        throw std::runtime_error("string too long in artifact");
    }
    std::string s(len, '\0');
    in.read(s.data(), static_cast<std::streamsize>(len));
    return s;
}

TypeInfo ModuleArtifact::read_type_info(std::istream& in) {
    TypeInfo t;
    t.kind = static_cast<TypeKind>(read_u8(in));
    t.name = read_str(in);
    t.is_let = read_bool(in);
    t.is_persist = read_bool(in);
    t.is_sync = read_bool(in);
    t.is_pub = read_bool(in);

    bool has_element = read_bool(in);
    if (has_element) {
        t.element = std::make_shared<TypeInfo>(read_type_info(in));
    }

    uint32_t param_count = read_u32(in);
    t.params.reserve(param_count);
    for (uint32_t i = 0; i < param_count; ++i) {
        t.params.push_back(read_type_info(in));
    }

    bool has_ret = read_bool(in);
    if (has_ret) {
        t.ret = std::make_shared<TypeInfo>(read_type_info(in));
    }

    return t;
}

std::unordered_map<std::string, ResolvedTrait> ModuleArtifact::read_traits(std::istream& in) {
    std::unordered_map<std::string, ResolvedTrait> traits;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedTrait trait;
        trait.name = read_str(in);
        trait.is_pub = read_bool(in);
        uint32_t field_count = read_u32(in);
        trait.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            ResolvedField field;
            field.name = read_str(in);
            field.is_let = read_bool(in);
            field.is_var = read_bool(in);
            field.is_persist = read_bool(in);
            field.is_sync = read_bool(in);
            field.is_pub = read_bool(in);
            field.type = read_type_info(in);
            trait.fields.push_back(std::move(field));
        }
        traits[trait.name] = std::move(trait);
    }
    return traits;
}

std::unordered_map<std::string, ResolvedStruct> ModuleArtifact::read_structs(std::istream& in) {
    std::unordered_map<std::string, ResolvedStruct> structs;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedStruct strct;
        strct.name = read_str(in);
        uint32_t field_count = read_u32(in);
        strct.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            ResolvedField field;
            field.name = read_str(in);
            field.is_let = read_bool(in);
            field.is_var = read_bool(in);
            field.is_persist = read_bool(in);
            field.is_sync = read_bool(in);
            field.is_pub = read_bool(in);
            field.type = read_type_info(in);
            strct.fields.push_back(std::move(field));
        }
        structs[strct.name] = std::move(strct);
    }
    return structs;
}

std::unordered_map<std::string, ResolvedEnum> ModuleArtifact::read_enums(std::istream& in) {
    std::unordered_map<std::string, ResolvedEnum> enums;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedEnum enm;
        enm.name = read_str(in);
        uint32_t var_count = read_u32(in);
        enm.variants.reserve(var_count);
        for (uint32_t j = 0; j < var_count; ++j) {
            enm.variants.push_back(read_str(in));
        }
        enums[enm.name] = std::move(enm);
    }
    return enums;
}

std::vector<SystemDependency> ModuleArtifact::read_dep_graph(std::istream& in) {
    std::vector<SystemDependency> graph;
    uint32_t count = read_u32(in);
    graph.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        SystemDependency dep;
        dep.system_name = read_str(in);

        uint32_t reads_count = read_u32(in);
        for (uint32_t j = 0; j < reads_count; ++j) dep.reads.insert(read_str(in));

        uint32_t writes_count = read_u32(in);
        for (uint32_t j = 0; j < writes_count; ++j) dep.writes.insert(read_str(in));

        uint32_t emits_count = read_u32(in);
        for (uint32_t j = 0; j < emits_count; ++j) dep.emits.insert(read_str(in));

        graph.push_back(std::move(dep));
    }
    return graph;
}

StringPool ModuleArtifact::read_string_pool(std::istream& in) {
    StringPool pool;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        auto s = read_str(in);
        pool.intern(s);
    }
    return pool;
}

// ── Save ─────────────────────────────────────────────────────────────────────

bool ModuleArtifact::save(const DecoratedProgram& program,
                           const std::string& module_name,
                           const fs::path& build_dir,
                           bool create_dir) {
    if (create_dir && !fs::exists(build_dir)) {
        std::error_code ec;
        fs::create_directories(build_dir, ec);
        if (ec) {
            errors_.error({}, "cannot create build dir: " + build_dir.string() + ": " + ec.message());
            return false;
        }
    }

    auto output = build_dir / artifact_filename(module_name);

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        errors_.error({}, "cannot write artifact: " + output.string());
        return false;
    }

    // Header
    out.write(MAGIC, 4);
    write_u8(out, CURRENT_VERSION);
    write_str(out, module_name);

    // Sections
    write_traits(out, program.traits);
    write_structs(out, program.structs);
    write_enums(out, program.enums);
    write_dep_graph(out, program.dependency_graph);
    write_string_pool(out, program.string_pool);

    return out.good();
}

// ── Load ─────────────────────────────────────────────────────────────────────

std::optional<DecoratedProgram> ModuleArtifact::load(const fs::path& path,
                                                       std::string& module_name_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors_.error({}, "cannot read artifact: " + path.string());
        return std::nullopt;
    }

    // Validate magic
    char magic[4] = {};
    in.read(magic, 4);
    if (std::memcmp(magic, MAGIC, 4) != 0) {
        errors_.error({}, "invalid artifact format in '" + path.filename().string() + "'");
        return std::nullopt;
    }

    // Validate version
    uint8_t version = read_u8(in);
    if (version != CURRENT_VERSION) {
        errors_.error({}, "incompatible module artifact version in '" +
                              path.filename().string() + "'; please recompile");
        return std::nullopt;
    }

    module_name_out = read_str(in);

    try {
        DecoratedProgram program;
        program.traits = read_traits(in);
        program.structs = read_structs(in);
        program.enums = read_enums(in);
        program.dependency_graph = read_dep_graph(in);
        program.string_pool = read_string_pool(in);
        program.ast = nullptr;  // not serialized

        if (!in.good()) {
            errors_.error({}, "truncated artifact: " + path.string());
            return std::nullopt;
        }

        return program;
    } catch (const std::exception& e) {
        errors_.error({}, "error reading artifact '" + path.string() + "': " + e.what());
        return std::nullopt;
    }
}

// ── Extract pub symbols ───────────────────────────────────────────────────────

std::optional<ImportedSymbols> ModuleArtifact::extract_pub_symbols(const fs::path& path) {
    std::string module_name;
    auto program = load(path, module_name);
    if (!program) return std::nullopt;

    ImportedSymbols symbols;
    symbols.module_name = module_name;

    for (auto& [name, trait] : program->traits) {
        if (trait.is_pub) {
            symbols.traits[name] = trait;
        }
    }
    for (auto& [name, strct] : program->structs) {
        // ResolvedStruct doesn't have is_pub field in current struct definition;
        // include all structs as potentially public (can be refined later)
        symbols.structs[name] = strct;
    }
    for (auto& [name, enm] : program->enums) {
        symbols.enums[name] = enm;
    }

    return symbols;
}

}  // namespace cactus
