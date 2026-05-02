#include "frontend/program_linker.hpp"

#include "frontend/module_artifact.hpp"

namespace cactus {

ProgramLinker::ProgramLinker(ErrorReporter& errors)
    : errors_(errors) {}

// ── 5.2: Incremental merge ───────────────────────────────────────────────────

bool ProgramLinker::merge_into(DecoratedProgram& target,
                               const DecoratedProgram& src,
                               const std::string& src_module_name) {
    if (merged_modules_.contains(src_module_name)) {
        return true;
    }

    bool ok = true;

    // ── Merge traits ─────────────────────────────────────────────────────────
    for (const auto& [name, trait] : src.traits) {
        // ── 5.3: Duplicate pub symbol detection ──────────────────────────────
        if (trait.is_pub) {
            auto it = symbol_origins_.find(name);
            if (it != symbol_origins_.end()) {
                std::string msg = "duplicate symbol '";
                msg += name;
                msg += "' defined in module '";
                msg += it->second;
                msg += "' and module '";
                msg += src_module_name;
                msg += "'";
                errors_.error({}, msg);
                ok = false;
                continue;
            }
            symbol_origins_[name] = src_module_name;
        }
        target.traits[name] = trait;
    }

    // ── Merge structs ────────────────────────────────────────────────────────
    for (const auto& [name, strct] : src.structs) {
        auto it = symbol_origins_.find(name);
        if (it != symbol_origins_.end()) {
            std::string msg = "duplicate symbol '";
            msg += name;
            msg += "' defined in module '";
            msg += it->second;
            msg += "' and module '";
            msg += src_module_name;
            msg += "'";
            errors_.error({}, msg);
            ok = false;
            continue;
        }
        symbol_origins_[name] = src_module_name;
        target.structs[name]  = strct;
    }

    // ── Merge enums ──────────────────────────────────────────────────────────
    for (const auto& [name, enm] : src.enums) {
        auto it = symbol_origins_.find(name);
        if (it != symbol_origins_.end()) {
            std::string msg = "duplicate symbol '";
            msg += name;
            msg += "' defined in module '";
            msg += it->second;
            msg += "' and module '";
            msg += src_module_name;
            msg += "'";
            errors_.error({}, msg);
            ok = false;
            continue;
        }
        symbol_origins_[name] = src_module_name;
        target.enums[name]    = enm;
    }

    // ── 5.4 + 5.2: Merge dependency graph (append) ──────────────────────────
    for (const auto& dep : src.dependency_graph) {
        target.dependency_graph.push_back(dep);
    }

    // ── 5.5: Merge string pool ────────────────────────────────────────────────
    // StringPool has no iteration API, so merging is done at the source level.
    // The linker integrates interned string names from declaration names instead.
    // Intern all known symbol names into the target pool as a practical merge.
    for (const auto& [name, _] : src.traits) {
        target.string_pool.intern(name);
    }
    for (const auto& [name, _] : src.structs) {
        target.string_pool.intern(name);
    }
    for (const auto& [name, _] : src.enums) {
        target.string_pool.intern(name);
    }
    for (const auto& dep : src.dependency_graph) {
        target.string_pool.intern(dep.system_name);
    }

    if (ok) {
        merged_modules_.insert(src_module_name);
    }
    return ok;
}

// ── 5.1: Link from artifact files ───────────────────────────────────────────

std::optional<DecoratedProgram> ProgramLinker::link(const std::vector<std::filesystem::path>& artifact_paths) {
    DecoratedProgram merged;
    merged.ast = nullptr;  // AST is not preserved in artifacts

    for (const auto& path : artifact_paths) {
        ErrorReporter artifact_errors;
        ModuleArtifact artifact(artifact_errors);

        std::string module_name;
        auto prog = artifact.load(path, module_name);

        if (!prog) {
            // Forward artifact load errors
            for (const auto& d : artifact_errors.diagnostics()) {
                errors_.error(d.location, d.message);
            }
            return std::nullopt;
        }

        if (!merge_into(merged, *prog, module_name)) {
            // merge_into already reported the duplicate error
            return std::nullopt;
        }
    }

    if (errors_.has_errors()) {
        return std::nullopt;
    }
    return merged;
}

}  // namespace cactus
