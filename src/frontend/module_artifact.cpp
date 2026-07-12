#include "frontend/module_artifact.hpp"

#include "frontend/symbol_identity.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace cactus {

namespace fs = std::filesystem;

ModuleArtifact::ModuleArtifact(ErrorReporter& errors)
    : errors_(errors) {}

// ── Artifact filename ───────────────────────────────────────────────────────

fs::path ModuleArtifact::artifact_filename(const std::string& module_name) {
    return {module_name + ".cmod"};
}

// ── Write helpers ───────────────────────────────────────────────────────────

void ModuleArtifact::write_u8(std::ostream& out, uint8_t v) {
    out.put(static_cast<char>(v));
}

void ModuleArtifact::write_u32(std::ostream& out, uint32_t v) {
    out.put(static_cast<char>(v & 0xFFU));
    out.put(static_cast<char>((v >> 8U) & 0xFFU));
    out.put(static_cast<char>((v >> 16U) & 0xFFU));
    out.put(static_cast<char>((v >> 24U) & 0xFFU));
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

    // Optional canonical symbol identity (for Enum/Struct field types)
    write_bool(out, t.symbol_id.has_value());
    if (t.symbol_id.has_value()) {
        write_u8(out, static_cast<uint8_t>(t.symbol_id->kind));
        write_str(out, t.symbol_id->module.name);
        write_str(out, t.symbol_id->local_name);
    }

    // For List: write element type
    bool has_element = (t.kind == TypeKind::List && t.element);
    write_bool(out, has_element);
    if (has_element) {
        write_type_info(out, *t.element);
    }

    // For Func: write params + return type
    write_u32(out, static_cast<uint32_t>(t.params.size()));
    for (const auto& p : t.params) {
        write_type_info(out, p);
    }
    bool has_ret = (t.kind == TypeKind::Func && t.ret);
    write_bool(out, has_ret);
    if (has_ret) {
        write_type_info(out, *t.ret);
    }
}

void ModuleArtifact::write_traits(std::ostream& out,
                                  const std::unordered_map<std::string, ResolvedTrait>& traits,
                                  const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(traits.size()));
    for (const auto& [name, trait] : traits) {
        write_str(out, trait.name);
        // Write canonical identity as stored; the linker derives it from src_module_name
        // when canonical_id is absent, so we must not silently derive here or the loader
        // would see a non-empty canonical_id and treat it as an explicit map key.
        const std::string& mod = trait.module_name.empty() ? module_name : trait.module_name;
        write_str(out, mod);
        write_str(out, trait.canonical_id);
        write_bool(out, trait.is_pub);
        write_bool(out, trait.is_stdlib);
        write_u32(out, static_cast<uint32_t>(trait.fields.size()));
        for (const auto& field : trait.fields) {
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

void ModuleArtifact::write_structs(std::ostream& out,
                                   const std::unordered_map<std::string, ResolvedStruct>& structs,
                                   const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(structs.size()));
    for (const auto& [name, strct] : structs) {
        write_str(out, strct.name);
        const std::string& mod = strct.module_name.empty() ? module_name : strct.module_name;
        write_str(out, mod);
        write_str(out, strct.canonical_id);
        write_u32(out, static_cast<uint32_t>(strct.fields.size()));
        for (const auto& field : strct.fields) {
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

void ModuleArtifact::write_enums(std::ostream& out,
                                 const std::unordered_map<std::string, ResolvedEnum>& enums,
                                 const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(enums.size()));
    for (const auto& [name, enm] : enums) {
        write_str(out, enm.name);
        const std::string& mod = enm.module_name.empty() ? module_name : enm.module_name;
        write_str(out, mod);
        write_str(out, enm.canonical_id);
        write_u32(out, static_cast<uint32_t>(enm.variants.size()));
        for (const auto& v : enm.variants) {
            write_str(out, v);
        }
    }
}

void ModuleArtifact::write_funcs(std::ostream& out,
                                 const std::unordered_map<std::string, ResolvedFunc>& funcs,
                                 const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(funcs.size()));
    for (const auto& [name, func] : funcs) {
        write_str(out, func.name);
        const std::string& mod = func.module_name.empty() ? module_name : func.module_name;
        write_str(out, mod);
        write_str(out, func.canonical_id);
        write_bool(out, func.is_pub);
        write_bool(out, func.is_extern);
        write_bool(out, func.is_stdlib);
        write_u32(out, static_cast<uint32_t>(func.params.size()));
        for (const auto& p : func.params) {
            write_str(out, p.name);
            write_type_info(out, p.type);
        }
        bool has_ret = func.return_type.has_value();
        write_bool(out, has_ret);
        if (has_ret) {
            write_type_info(out, *func.return_type);
        }
    }
}

void ModuleArtifact::write_string_set(std::ostream& out, const std::unordered_set<std::string>& values) {
    ModuleArtifact::write_u32(out, static_cast<uint32_t>(values.size()));
    for (const auto& value : values) {
        ModuleArtifact::write_str(out, value);
    }
}

void ModuleArtifact::write_dep_graph(std::ostream& out, const std::vector<SystemDependency>& graph) {
    write_u32(out, static_cast<uint32_t>(graph.size()));
    for (const auto& dep : graph) {
        write_str(out, dep.system_name);
        // reads
        write_u32(out, static_cast<uint32_t>(dep.reads.size()));
        for (const auto& r : dep.reads) {
            write_str(out, r);
        }
        // writes
        write_u32(out, static_cast<uint32_t>(dep.writes.size()));
        for (const auto& w : dep.writes) {
            write_str(out, w);
        }
        // emits
        write_u32(out, static_cast<uint32_t>(dep.emits.size()));
        for (const auto& e : dep.emits) {
            write_str(out, e);
        }
        // after_systems (canonical system IDs, task 4.3)
        write_u32(out, static_cast<uint32_t>(dep.after_systems.size()));
        for (const auto& a : dep.after_systems) {
            write_str(out, a);
        }
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
    return static_cast<uint8_t>(in.get());
}

uint32_t ModuleArtifact::read_u32(std::istream& in) {
    const uint32_t B0 = static_cast<uint8_t>(in.get());
    const uint32_t B1 = static_cast<uint8_t>(in.get());
    const uint32_t B2 = static_cast<uint8_t>(in.get());
    const uint32_t B3 = static_cast<uint8_t>(in.get());
    return B0 | (B1 << 8U) | (B2 << 16U) | (B3 << 24U);
}

bool ModuleArtifact::read_bool(std::istream& in) {
    return read_u8(in) != 0;
}

std::string ModuleArtifact::read_str(std::istream& in) {
    uint32_t len = read_u32(in);
    if (len > 1024U * 1024U) {
        in.setstate(std::ios::failbit);
        return {};
    }
    std::string s(len, '\0');
    in.read(s.data(), static_cast<std::streamsize>(len));
    return s;
}

TypeInfo ModuleArtifact::read_type_info(std::istream& in) {
    TypeInfo t;
    t.kind       = static_cast<TypeKind>(read_u8(in));
    t.name       = read_str(in);
    t.is_let     = read_bool(in);
    t.is_persist = read_bool(in);
    t.is_sync    = read_bool(in);
    t.is_pub     = read_bool(in);

    bool has_symbol_id = read_bool(in);
    if (has_symbol_id) {
        SymbolId sym;
        sym.kind        = static_cast<SymbolKind>(read_u8(in));
        sym.module.name = read_str(in);
        sym.local_name  = read_str(in);
        t.symbol_id     = sym;
    }

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
        trait.name           = read_str(in);
        trait.module_name    = read_str(in);
        trait.canonical_id   = read_str(in);
        trait.is_pub         = read_bool(in);
        trait.is_stdlib      = read_bool(in);
        uint32_t field_count = read_u32(in);
        trait.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            ResolvedField field;
            field.name       = read_str(in);
            field.is_let     = read_bool(in);
            field.is_var     = read_bool(in);
            field.is_persist = read_bool(in);
            field.is_sync    = read_bool(in);
            field.is_pub     = read_bool(in);
            field.type       = read_type_info(in);
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
        strct.name           = read_str(in);
        strct.module_name    = read_str(in);
        strct.canonical_id   = read_str(in);
        uint32_t field_count = read_u32(in);
        strct.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            ResolvedField field;
            field.name       = read_str(in);
            field.is_let     = read_bool(in);
            field.is_var     = read_bool(in);
            field.is_persist = read_bool(in);
            field.is_sync    = read_bool(in);
            field.is_pub     = read_bool(in);
            field.type       = read_type_info(in);
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
        enm.name           = read_str(in);
        enm.module_name    = read_str(in);
        enm.canonical_id   = read_str(in);
        uint32_t var_count = read_u32(in);
        enm.variants.reserve(var_count);
        for (uint32_t j = 0; j < var_count; ++j) {
            enm.variants.push_back(read_str(in));
        }
        enums[enm.name] = std::move(enm);
    }
    return enums;
}

std::unordered_map<std::string, ResolvedFunc> ModuleArtifact::read_funcs(std::istream& in) {
    std::unordered_map<std::string, ResolvedFunc> funcs;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedFunc func;
        func.name            = read_str(in);
        func.module_name     = read_str(in);
        func.canonical_id    = read_str(in);
        func.is_pub          = read_bool(in);
        func.is_extern       = read_bool(in);
        func.is_stdlib       = read_bool(in);
        uint32_t param_count = read_u32(in);
        func.params.reserve(param_count);
        for (uint32_t j = 0; j < param_count; ++j) {
            ResolvedParam p;
            p.name = read_str(in);
            p.type = read_type_info(in);
            func.params.push_back(std::move(p));
        }
        bool has_ret = read_bool(in);
        if (has_ret) {
            func.return_type = read_type_info(in);
        }
        funcs[func.name] = std::move(func);
    }
    return funcs;
}

std::unordered_set<std::string> ModuleArtifact::read_string_set(std::istream& in) {
    std::unordered_set<std::string> values;
    uint32_t count = ModuleArtifact::read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        values.insert(ModuleArtifact::read_str(in));
    }
    return values;
}

std::vector<SystemDependency> ModuleArtifact::read_dep_graph(std::istream& in) {
    std::vector<SystemDependency> graph;
    uint32_t count = read_u32(in);
    graph.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        SystemDependency dep;
        dep.system_name = read_str(in);

        uint32_t reads_count = read_u32(in);
        for (uint32_t j = 0; j < reads_count; ++j) {
            dep.reads.insert(read_str(in));
        }

        uint32_t writes_count = read_u32(in);
        for (uint32_t j = 0; j < writes_count; ++j) {
            dep.writes.insert(read_str(in));
        }

        uint32_t emits_count = read_u32(in);
        for (uint32_t j = 0; j < emits_count; ++j) {
            dep.emits.insert(read_str(in));
        }

        uint32_t after_count = read_u32(in);
        dep.after_systems.reserve(after_count);
        for (uint32_t j = 0; j < after_count; ++j) {
            dep.after_systems.push_back(read_str(in));
        }

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
    write_traits(out, program.traits, module_name);
    write_structs(out, program.structs, module_name);
    write_enums(out, program.enums, module_name);
    write_funcs(out, program.funcs, module_name);
    write_string_set(out, program.pub_templates);
    write_string_set(out, program.non_pub_templates);
    write_dep_graph(out, program.dependency_graph);
    write_string_pool(out, program.string_pool);

    return out.good();
}

// ── Load ─────────────────────────────────────────────────────────────────────

std::optional<DecoratedProgram> ModuleArtifact::load(const fs::path& path, std::string& module_name_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors_.error({}, "cannot read artifact: " + path.string());
        return std::nullopt;
    }

    // Validate magic
    std::array<char, 4> magic = {};
    in.read(magic.data(), 4);
    if (std::memcmp(magic.data(), MAGIC, 4) != 0) {
        errors_.error({}, "invalid artifact format in '" + path.filename().string() + "'");
        return std::nullopt;
    }

    // Validate version
    uint8_t version = read_u8(in);
    if (version != CURRENT_VERSION) {
        errors_.error({},
                      "incompatible module artifact version in '" + path.filename().string() + "'; please recompile");
        return std::nullopt;
    }

    module_name_out = read_str(in);

    DecoratedProgram program;
    program.traits            = read_traits(in);
    program.structs           = read_structs(in);
    program.enums             = read_enums(in);
    program.funcs             = read_funcs(in);
    program.pub_templates     = read_string_set(in);
    program.non_pub_templates = read_string_set(in);
    program.dependency_graph  = read_dep_graph(in);
    program.string_pool       = read_string_pool(in);
    program.ast               = nullptr;  // not serialized

    if (!in.good()) {
        errors_.error({}, "truncated artifact: " + path.string());
        return std::nullopt;
    }

    return program;
}

// ── Extract pub symbols ───────────────────────────────────────────────────────

std::optional<ImportedSymbols> ModuleArtifact::extract_pub_symbols(const fs::path& path) {
    std::string module_name;
    auto program = load(path, module_name);
    if (!program) {
        return std::nullopt;
    }

    ImportedSymbols symbols;
    symbols.module_name = module_name;

    for (auto& [name, trait] : program->traits) {
        if (trait.is_pub) {
            // canonical_id is populated during load (read_traits). Ensure non-empty.
            if (trait.canonical_id.empty()) {
                trait.canonical_id = make_canonical_id(module_name, trait.name);
            }
            if (trait.module_name.empty()) {
                trait.module_name = module_name;
            }
            if (!trait.symbol_id.has_value()) {
                trait.symbol_id = make_symbol_id(SymbolKind::Trait, trait.module_name, trait.name);
            }
            symbols.traits[name] = trait;
        }
    }
    for (auto& [name, strct] : program->structs) {
        if (strct.canonical_id.empty()) {
            strct.canonical_id = make_canonical_id(module_name, strct.name);
        }
        if (strct.module_name.empty()) {
            strct.module_name = module_name;
        }
        if (!strct.symbol_id.has_value()) {
            strct.symbol_id = make_symbol_id(SymbolKind::Struct, strct.module_name, strct.name);
        }
        symbols.structs[name] = strct;
    }
    for (auto& [name, enm] : program->enums) {
        if (enm.canonical_id.empty()) {
            enm.canonical_id = make_canonical_id(module_name, enm.name);
        }
        if (enm.module_name.empty()) {
            enm.module_name = module_name;
        }
        if (!enm.symbol_id.has_value()) {
            enm.symbol_id = make_symbol_id(SymbolKind::Enum, enm.module_name, enm.name);
        }
        symbols.enums[name] = enm;
    }
    for (auto& [name, func] : program->funcs) {
        if (func.is_pub) {
            if (func.canonical_id.empty()) {
                func.canonical_id = make_canonical_id(module_name, func.name);
            }
            if (func.module_name.empty()) {
                func.module_name = module_name;
            }
            if (!func.symbol_id.has_value()) {
                func.symbol_id = make_symbol_id(SymbolKind::Func, func.module_name, func.name);
            }
            symbols.funcs[name] = func;
        }
    }

    for (const auto& tmpl_name : program->pub_templates) {
        const auto symbol = make_symbol_id(SymbolKind::Template, module_name, tmpl_name);
        ImportedTemplate tmpl;
        tmpl.name                    = symbol.local_name;
        tmpl.module_name             = symbol.module.name;
        tmpl.canonical_id            = make_canonical_id(symbol);
        tmpl.symbol_id               = symbol;
        symbols.templates[tmpl_name] = tmpl;
    }

    for (const auto& dep : program->dependency_graph) {
        const auto symbol = make_symbol_id(SymbolKind::System, module_name, dep.system_name);
        ImportedSystem sys;
        sys.name                         = symbol.local_name;
        sys.module_name                  = symbol.module.name;
        sys.canonical_id                 = make_canonical_id(symbol);
        sys.symbol_id                    = symbol;
        sys.after_systems                = dep.after_systems;
        symbols.systems[dep.system_name] = sys;
    }

    return symbols;
}

}  // namespace cactus
