#include "backends/cpp-manual/cpp_manual_codegen.h"

#include "backends/cpp-manual/event_emitter.h"
#include "backends/cpp-manual/soa_emitter.h"
#include "backends/cpp-manual/system_emitter.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace cactus {

namespace {

// Task 7.1: Check if the program has any extern funcs requiring the runtime header
bool has_extern_funcs(const DecoratedProgram& program) {
    for (const auto& [name, func] : program.funcs) { // NOLINT(readability-use-anyofallof)
        if (func.is_extern) {
            return true;
        }
    }
    return false;
}

// ── Build CodegenContext from a DecoratedProgram ────────────────────────────

CodegenContext build_context(const DecoratedProgram& program, // NOLINT(readability-function-cognitive-complexity)
                              const std::vector<std::string>& trait_names_ordered) {
    CodegenContext ctx;
    ctx.ast = program.ast;
    ctx.traits = program.traits;
    ctx.trait_names_ordered = trait_names_ordered;
    for (size_t i = 0; i < trait_names_ordered.size(); ++i) {
        ctx.trait_bit_index[trait_names_ordered[i]] = static_cast<int>(i);
    }

    if (program.ast == nullptr) {
        return ctx;
    }

    for (const auto& decl : program.ast->declarations) {
        std::visit(
            [&ctx](const auto& node) { // NOLINT(readability-function-cognitive-complexity)
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, TraitNode>) {
                    ctx.trait_ast[node.name] = &node;
                    for (const auto& f : node.fields) {
                        if (f.default_value) {
                            ctx.trait_defaults[node.name][f.name] =
                                ManualSystemEmitter::emit_expr(**f.default_value);
                        }
                    }
                } else if constexpr (std::is_same_v<T, TemplateNode>) {
                    ctx.template_ast[node.name] = &node;
                    for (const auto& trait : node.traits) {
                        for (const auto& a : trait.assignments) {
                            ctx.template_config[node.name][a.name] =
                                ManualSystemEmitter::emit_expr(*a.value);
                        }
                    }
                } else if constexpr (std::is_same_v<T, UnitNode>) {
                    ctx.unit_ast[node.name] = &node;
                }
            },
            decl);
    }

    return ctx;
}

// ── Emit template factory function (task 7.7) ─────────────────────────────

std::string emit_template_factory(const TemplateNode& tmpl, const CodegenContext& ctx) { // NOLINT(readability-function-cognitive-complexity)
    std::ostringstream out;

    // Collect factory parameters: one per field of each declared trait, in order
    struct Param {
        std::string cpp_type;
        std::string param_name;
        std::string default_val;
        std::string trait_name;
        std::string field_name;
    };
    std::vector<Param> params;

    for (const auto& entry : tmpl.traits) {
        auto tit = ctx.traits.find(entry.trait_name);
        if (tit == ctx.traits.end()) {
            continue;
        }
        for (const auto& field : tit->second.fields) {
            Param p;
            p.cpp_type   = SoaEmitter::type_to_cpp(field.type);
            p.param_name = "p_" + entry.trait_name + "_" + field.name;
            p.trait_name = entry.trait_name;
            p.field_name = field.name;

            // Default: template nested assignment → trait default → type default
            for (const auto& assign : entry.assignments) {
                if (assign.name == field.name) {
                    p.default_val = ManualSystemEmitter::emit_expr(*assign.value);
                    break;
                }
            }
            if (p.default_val.empty()) {
                auto td_it = ctx.trait_defaults.find(entry.trait_name);
                if (td_it != ctx.trait_defaults.end()) {
                    auto fi = td_it->second.find(field.name);
                    if (fi != td_it->second.end()) {
                        p.default_val = fi->second;
                    }
                }
            }
            if (p.default_val.empty()) {
                p.default_val = SoaEmitter::default_cpp_value(field.type);
            }

            params.push_back(std::move(p));
        }
    }

    // Compute initial trait_mask (all listed traits are active at spawn)
    std::string mask_expr = "0ULL";
    for (const auto& entry : tmpl.traits) {
        auto it = ctx.trait_bit_index.find(entry.trait_name);
        if (it != ctx.trait_bit_index.end()) {
            mask_expr += " | TraitBits::" + entry.trait_name;
        }
    }

    out << "static void spawn_" << tmpl.name << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ",\n    ";
        } else {
            out << "\n    ";
        }
        out << params[i].cpp_type << " " << params[i].param_name
            << " = " << params[i].default_val;
    }
    if (!params.empty()) {
        out << "\n";
    }
    out << ") {\n";
    out << "    size_t _idx = entity_count++;\n";
    out << "    g_trait_mask[_idx] = " << mask_expr << ";\n";
    for (const auto& p : params) {
        out << "    g_" << p.trait_name << "_" << p.field_name
            << "[_idx] = " << p.param_name << ";\n";
    }
    out << "    dispatch_on_spawn(_idx);\n";
    out << "}\n";
    return out.str();
}

// ── Compute field value for unit initialization ────────────────────────────

std::string field_init_value(const std::string& trait_name, const std::string& field_name,
                              const UnitNode& unit, const CodegenContext& ctx) {
    // 1. Unit nested trait assignment
    for (const auto& trait : unit.traits) {
        if (trait.trait_name != trait_name) {
            continue;
        }
        for (const auto& a : trait.assignments) {
            if (a.name == field_name) {
                return ManualSystemEmitter::emit_expr(*a.value);
            }
        }
    }
    // 2. Trait field default
    auto td_it = ctx.trait_defaults.find(trait_name);
    if (td_it != ctx.trait_defaults.end()) {
        auto fi = td_it->second.find(field_name);
        if (fi != td_it->second.end()) {
            return fi->second;
        }
    }
    // 3. Type default
    auto tit = ctx.traits.find(trait_name);
    if (tit != ctx.traits.end()) {
        for (const auto& f : tit->second.fields) {
            if (f.name == field_name) {
                return SoaEmitter::default_cpp_value(f.type);
            }
        }
    }
    return "{}";
}

}  // namespace

// ── CppManualCodegen::generate ─────────────────────────────────────────────

std::string CppManualCodegen::generate(const DecoratedProgram& program) { // NOLINT(readability-function-cognitive-complexity)
    if (program.ast == nullptr) {
        return "// Generated by Cactus DSL Compiler (cpp-manual backend)\n";
    }

    std::ostringstream out;

    // ── Step 1: Collect trait names in declaration order ───────────────────
    std::vector<std::string> trait_names_ordered;
    std::vector<const TemplateNode*> templates;
    std::vector<const UnitNode*>     units;
    std::vector<const SystemNode*>   systems;

    for (const auto& decl : program.ast->declarations) {
        std::visit(
            [&](const auto& node) { // NOLINT(readability-function-cognitive-complexity)
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, TraitNode>) {
                    trait_names_ordered.push_back(node.name);
                } else if constexpr (std::is_same_v<T, TemplateNode>) {
                    templates.push_back(&node);
                } else if constexpr (std::is_same_v<T, UnitNode>) {
                    units.push_back(&node);
                } else if constexpr (std::is_same_v<T, SystemNode>) {
                    systems.push_back(&node);
                }
            },
            decl);
    }

    // ── Step 2: Build context ──────────────────────────────────────────────
    CodegenContext ctx = build_context(program, trait_names_ordered);

    // ── Step 3: Header ─────────────────────────────────────────────────────
    out << "// Generated by Cactus DSL Compiler (cpp-manual backend)\n";
    out << "#pragma once\n\n";
    out << "#include <cstdint>\n";
    out << "#include <string>\n";
    out << "#include <vector>\n";
    out << "#include \"raylib.h\"\n";
    // Task 7.2: Include runtime header when extern funcs are present
    if (has_extern_funcs(program)) {
        out << "#include \"cactus_runtime.h\"\n";
    }
    out << "\n";

    // ── Step 4: Enums and structs ──────────────────────────────────────────
    for (const auto& [name, e] : program.enums) {
        out << SoaEmitter::emit_enum(e) << "\n";
    }
    for (const auto& [name, s] : program.structs) {
        out << SoaEmitter::emit_pod_struct(s) << "\n";
    }

    // ── Step 5: TraitBits (task 7.1) ───────────────────────────────────────
    out << SoaEmitter::emit_trait_bits(trait_names_ordered) << "\n";

    // ── Step 6: Global entity pool (task 7.2) ─────────────────────────────
    out << SoaEmitter::emit_global_entity_pool() << "\n";
    out << SoaEmitter::emit_global_field_arrays(program.traits, trait_names_ordered) << "\n";

    // ── Step 7: Deferred load state (task 8.1) ────────────────────────────
    out << "// ── Deferred Load ────────────────────────────────────────────────────\n";
    out << "static std::string g_pending_load;\n";
    out << "static bool g_load_pending = false;\n";
    out << "static bool g_load_multi_error = false;\n\n";

    // ── Step 8: Events ─────────────────────────────────────────────────────
    for (const auto& decl : program.ast->declarations) {
        if (const auto* event = std::get_if<EventNode>(&decl)) {
            out << ManualEventEmitter::emit_event(*event, program);
            out << ManualEventEmitter::emit_dispatch(*event);
        }
    }

    // ── Step 9: Forward declarations ──────────────────────────────────────
    out << "// ── Forward Declarations ─────────────────────────────────────────────\n";
    for (const auto* sys : systems) {
        out << ManualSystemEmitter::emit_system_forward_decls(*sys);
    }
    out << "\n";

    // ── Step 10: on_spawn dispatch (task 7.11) ────────────────────────────
    out << "// ── on_spawn dispatch ───────────────────────────────────────────────\n";
    out << "static void dispatch_on_spawn(size_t _idx) {\n";
    for (const auto* sys : systems) {
        for (const auto& h : sys->handlers) {
            if (h.event_name != "spawn") {
                continue;
            }
            const std::string FM = ManualSystemEmitter::compute_mask_expr(sys->filter, ctx);
            const std::string EM = ManualSystemEmitter::compute_mask_expr(sys->exclude, ctx);
            out << "    if ((g_trait_mask[_idx] & (" << FM << ")) == (" << FM << ") &&\n";
            out << "        (g_trait_mask[_idx] & (" << EM << ")) == 0) {\n";
            out << "        " << sys->name << "_spawn(_idx, SpawnEvent{});\n";
            out << "    }\n";
        }
    }
    out << "}\n\n";

    // ── Step 11: on_destroy dispatch (task 7.12) ──────────────────────────
    out << "// ── on_destroy dispatch ─────────────────────────────────────────────\n";
    out << "static void dispatch_on_destroy(size_t _idx) {\n";
    for (const auto* sys : systems) {
        for (const auto& h : sys->handlers) {
            if (h.event_name != "destroy") {
                continue;
            }
            const std::string FM = ManualSystemEmitter::compute_mask_expr(sys->filter, ctx);
            const std::string EM = ManualSystemEmitter::compute_mask_expr(sys->exclude, ctx);
            out << "    if ((g_trait_mask[_idx] & (" << FM << ")) == (" << FM << ") &&\n";
            out << "        (g_trait_mask[_idx] & (" << EM << ")) == 0) {\n";
            out << "        " << sys->name << "_destroy(_idx, DestroyEvent{});\n";
            out << "    }\n";
        }
    }
    out << "}\n\n";

    // ── Step 12: entity_remove (swap-and-delete) (task 7.9) ───────────────
    out << "// ── Entity Remove (swap-and-delete) ─────────────────────────────────\n";
    out << "static void entity_remove(size_t _idx) {\n";
    out << "    dispatch_on_destroy(_idx);\n";
    out << "    const size_t _last = entity_count - 1;\n";
    out << "    if (_idx != _last) {\n";
    out << "        g_trait_mask[_idx] = g_trait_mask[_last];\n";
    for (const auto& name : trait_names_ordered) {
        auto tit = program.traits.find(name);
        if (tit == program.traits.end()) {
            continue;
        }
        for (const auto& f : tit->second.fields) {
            out << "        g_" << name << "_" << f.name << "[_idx] = "
                << "g_" << name << "_" << f.name << "[_last];\n";
        }
    }
    out << "    }\n";
    out << "    --entity_count;\n";
    out << "}\n\n";

    // ── Step 13: Template factory functions (task 7.7) ────────────────────
    if (!templates.empty()) {
        out << "// ── Template Factories ──────────────────────────────────────────────\n";
        for (const auto* tmpl : templates) {
            out << emit_template_factory(*tmpl, ctx) << "\n";
        }
    }

    // ── Step 14: System handler functions (tasks 7.3–7.6, 7.13, 7.14) ────
    out << "// ── Systems ─────────────────────────────────────────────────────────\n\n";
    for (const auto* sys : systems) {
        out << ManualSystemEmitter::emit_system_dynamic(*sys, ctx);
    }

    // ── Step 15: on_unload dispatch (task 7.13 / 8.2) ────────────────────
    out << "// ── on_unload dispatch ──────────────────────────────────────────────\n";
    out << "static void dispatch_on_unload() {\n";
    for (const auto* sys : systems) {
        for (const auto& h : sys->handlers) {
            if (h.event_name == "unload") {
                out << "    " << sys->name << "_unload(UnloadEvent{});\n";
            }
        }
    }
    out << "}\n\n";

    // ── Step 16: on_load dispatch (task 7.14 / 8.4) ───────────────────────
    out << "// ── on_load dispatch ────────────────────────────────────────────────\n";
    out << "static void dispatch_on_load() {\n";
    for (const auto* sys : systems) {
        for (const auto& h : sys->handlers) {
            if (h.event_name == "load") {
                out << "    " << sys->name << "_load(LoadEvent{});\n";
            }
        }
    }
    out << "}\n\n";

    // ── Step 17: perform_load (3-phase scene transition) (tasks 8.2–8.4) ──
    out << "// ── Scene Loading (3-phase transition) ──────────────────────────────\n";
    out << "static void perform_load(const std::string& _module_name) {\n";
    out << "    (void)_module_name;\n";
    out << "    // Phase 1 — Unload (task 8.2): fires on_unload() on all active systems\n";
    out << "    dispatch_on_unload();\n";
    out << "    // Phase 2 — Instantiate (task 8.3): load _data.bin, emit on_spawn() per entity\n";
    out << "    // (Runtime data-file loading is handled externally; "
           "entities spawned by on_load)\n";
    out << "    // Phase 3 — Load (task 8.4): fires on_load() on all active systems\n";
    out << "    dispatch_on_load();\n";
    out << "}\n\n";

    // ── Step 18: Persist/sync stubs ────────────────────────────────────────
    out << "// ── Persist Serialization ────────────────────────────────────────────\n\n";
    for (const auto& [name, t] : program.traits) {
        bool has_persist = false;
        for (const auto& f : t.fields) {
            if (f.is_persist) { has_persist = true; break; }
        }
        if (has_persist) {
            out << "void save_" << name << "(size_t idx) {\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // serialize g_" << name << "_" << f.name << "[idx]\n";
                }
            }
            out << "}\n\n";
            out << "void load_" << name << "(size_t idx) {\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // deserialize into g_" << name << "_" << f.name << "[idx]\n";
                }
            }
            out << "}\n\n";
        }
    }

    // ── Step 19: Sync replication stubs ────────────────────────────────────
    out << "// ── Sync Replication ─────────────────────────────────────────────────\n\n";
    for (const auto& [name, t] : program.traits) {
        bool has_sync = false;
        for (const auto& f : t.fields) {
            if (f.is_sync) { has_sync = true; break; }
        }
        if (has_sync) {
            out << "void replicate_" << name << "(size_t idx) {\n";
            for (const auto& f : t.fields) {
                if (f.is_sync) {
                    out << "    // send delta for g_" << name << "_" << f.name << "[idx]\n";
                }
            }
            out << "}\n\n";
        }
    }

    // ── Step 20: Unit initialization function ─────────────────────────────
    out << "// ── Unit Initialization ─────────────────────────────────────────────\n";
    out << "static void init_units() {\n";
    for (const auto* unit : units) {
        out << "    // Unit: " << unit->name << "\n";
        out << "    {\n";
        out << "        size_t _idx = entity_count++;\n";
        std::string mask = "0ULL";
        for (const auto& entry : unit->traits) {
            auto it = ctx.trait_bit_index.find(entry.trait_name);
            if (it != ctx.trait_bit_index.end()) {
                mask += " | TraitBits::" + entry.trait_name;
            }
        }
        out << "        g_trait_mask[_idx] = " << mask << ";\n";
        // Initialize fields
        for (const auto& entry : unit->traits) {
            auto tit = ctx.traits.find(entry.trait_name);
            if (tit == ctx.traits.end()) {
                continue;
            }
            for (const auto& field : tit->second.fields) {
                out << "        g_" << entry.trait_name << "_" << field.name << "[_idx] = "
                    << field_init_value(entry.trait_name, field.name, *unit, ctx) << ";\n";
            }
        }
        out << "        dispatch_on_spawn(_idx);\n";
        out << "    }\n";
    }
    out << "}\n\n";

    // ── Step 21: Main game loop ────────────────────────────────────────────
    out << "// ── Main ─────────────────────────────────────────────────────────────\n\n";
    out << "int main() {\n";
    out << "    InitWindow(800, 600, \"Cactus Game\");\n";
    out << "    SetTargetFPS(60);\n\n";
    out << "    init_units();\n\n";
    out << "    while (!WindowShouldClose()) {\n";
    out << "        float dt = GetFrameTime();\n\n";

    // Call system tick handlers
    for (const auto* sys : systems) {
        for (const auto& handler : sys->handlers) {
            if (handler.event_name == "tick") {
                out << "        " << sys->name << "_tick(TickEvent{dt});\n";
            }
        }
    }

    // End-of-frame deferred load (task 8.1)
    out << "\n";
    out << "        // End-of-frame deferred load (task 8.1)\n";
    out << "        if (g_load_pending) {\n";
    out << "            if (g_load_multi_error) {\n";
    out << "                // Error: multiple `load` calls in a single frame\n";
    out << "            }\n";
    out << "            perform_load(g_pending_load);\n";
    out << "            g_pending_load.clear();\n";
    out << "            g_load_pending = false;\n";
    out << "            g_load_multi_error = false;\n";
    out << "        }\n";
    out << "\n";
    out << "        BeginDrawing();\n";
    out << "        ClearBackground(RAYWHITE);\n";
    out << "        EndDrawing();\n";
    out << "    }\n\n";
    out << "    CloseWindow();\n";
    out << "    return 0;\n";
    out << "}\n";

    return out.str();
}

}  // namespace cactus
