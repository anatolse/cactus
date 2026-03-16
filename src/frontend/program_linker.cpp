#include "frontend/program_linker.h"

#include "frontend/module_artifact.h"

namespace cactus {

ProgramLinker::ProgramLinker(ErrorReporter& errors) : errors_(errors) {}

// ── 5.2: Incremental merge ───────────────────────────────────────────────────

bool ProgramLinker::merge_into(DecoratedProgram& target, const DecoratedProgram& src,
                                const std::string& src_module_name) {
    bool ok = true;

    // ── Merge traits ─────────────────────────────────────────────────────────
    for (auto& [name, trait] : src.traits) {
        // ── 5.3: Duplicate pub symbol detection ──────────────────────────────
        if (trait.is_pub) {
            auto it = symbol_origins_.find(name);
            if (it != symbol_origins_.end()) {
                errors_.error({}, "duplicate symbol '" + name + "' defined in module '" +
                                      it->second + "' and module '" + src_module_name + "'");
                ok = false;
                continue;
            }
            symbol_origins_[name] = src_module_name;
        }
        target.traits[name] = trait;
    }

    // ── Merge structs ────────────────────────────────────────────────────────
    for (auto& [name, strct] : src.structs) {
        auto it = symbol_origins_.find(name);
        if (it != symbol_origins_.end()) {
            errors_.error({}, "duplicate symbol '" + name + "' defined in module '" +
                                  it->second + "' and module '" + src_module_name + "'");
            ok = false;
            continue;
        }
        symbol_origins_[name] = src_module_name;
        target.structs[name] = strct;
    }

    // ── Merge enums ──────────────────────────────────────────────────────────
    for (auto& [name, enm] : src.enums) {
        auto it = symbol_origins_.find(name);
        if (it != symbol_origins_.end()) {
            errors_.error({}, "duplicate symbol '" + name + "' defined in module '" +
                                  it->second + "' and module '" + src_module_name + "'");
            ok = false;
            continue;
        }
        symbol_origins_[name] = src_module_name;
        target.enums[name] = enm;
    }

    // ── 5.4 + 5.2: Merge dependency graph (append) ──────────────────────────
    for (auto& dep : src.dependency_graph) {
        target.dependency_graph.push_back(dep);
    }

    // ── 5.5: Merge string pool ────────────────────────────────────────────────
    // StringPool has no iteration API, so merging is done at the source level.
    // The linker integrates interned string names from declaration names instead.
    // Intern all known symbol names into the target pool as a practical merge.
    for (auto& [name, _] : src.traits)  target.string_pool.intern(name);
    for (auto& [name, _] : src.structs) target.string_pool.intern(name);
    for (auto& [name, _] : src.enums)   target.string_pool.intern(name);
    for (auto& dep : src.dependency_graph) {
        target.string_pool.intern(dep.system_name);
    }

    return ok;
}

// ── 5.1: Link from artifact files ───────────────────────────────────────────

std::optional<DecoratedProgram> ProgramLinker::link(
    const std::vector<std::filesystem::path>& artifact_paths) {
    DecoratedProgram merged;
    merged.ast = nullptr;  // AST is not preserved in artifacts

    for (auto& path : artifact_paths) {
        ErrorReporter artifact_errors;
        ModuleArtifact artifact(artifact_errors);

        std::string module_name;
        auto prog = artifact.load(path, module_name);

        if (!prog) {
            // Forward artifact load errors
            for (auto& d : artifact_errors.diagnostics()) {
                errors_.error(d.location, d.message);
            }
            return std::nullopt;
        }

        if (!merge_into(merged, *prog, module_name)) {
            // merge_into already reported the duplicate error
            return std::nullopt;
        }
    }

    if (errors_.has_errors()) return std::nullopt;
    return merged;
}

}  // namespace cactus
