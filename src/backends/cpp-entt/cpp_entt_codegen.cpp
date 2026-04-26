#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/event_emitter.hpp"
#include "backends/cpp-entt/system_emitter.hpp"
#include "backends/cpp-manual/soa_emitter.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace cactus {

namespace {
// Task 6.1: Check if the program has any extern funcs requiring the runtime header
bool has_extern_funcs(const DecoratedProgram& program) {
    for (const auto& [name, func] : program.funcs) {  // NOLINT(readability-use-anyofallof)
        if (func.is_extern) {
            return true;
        }
    }
    return false;
}

std::string upper_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string snake_case(const std::string& value) {
    std::string result;
    for (char ch : value) {
        if (std::isupper(static_cast<unsigned char>(ch)) != 0) {
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else {
            result += ch;
        }
    }
    return result;
}

std::string system_function_name(const std::string& system_name, const std::string& suffix) {
    return snake_case(system_name) + "_" + suffix;
}

std::string input_action_constant_name(const std::string& input_name) {
    return "K_" + upper_copy(snake_case(input_name));
}

std::string cpp_string_literal(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string asset_register_call(const AssetDeclNode& asset) {
    const auto path_literal = cpp_string_literal(asset.path);
    switch (asset.asset_kind) {
        case AssetKind::Mesh:
            return "shared_asset_registry().register_mesh(" + asset.name + ", " + path_literal + ", static_cast<int>(" +
                   asset.name + "));";
        case AssetKind::Texture:
            return "shared_asset_registry().register_texture(" + asset.name + ", " + path_literal +
                   ", static_cast<int>(" + asset.name + "));";
        case AssetKind::Material:
            return "shared_asset_registry().register_material(" + asset.name + ", " + path_literal +
                   ", static_cast<int>(" + asset.name + "));";
        case AssetKind::Sound:
        case AssetKind::Music:
        case AssetKind::Font:
            return {};
    }
    return {};
}

std::string filter_simple_name(const FilterEntry& entry) {
    auto dot = entry.qualified_name.rfind('.');
    return (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
}

bool filter_has_trait(const FilterClause& filter, const std::string& qualified, const std::string& simple) {
    for (const auto& entry : filter.entries) {
        if (entry.qualified_name == qualified || filter_simple_name(entry) == simple) {
            return true;
        }
    }
    return std::ranges::find(filter.trait_names, simple) != filter.trait_names.end();
}

bool uses_stdlib_extern_contract(const ExternSystemNode& sys) {
    if (sys.is_stdlib) {
        return true;
    }
    if (sys.name == "TransformPropagation" || sys.name == "ShapeRenderer" || sys.name == "SpriteRenderer" ||
        sys.name == "AnimatedSpriteSystem" || sys.name == "MeshRenderer" || sys.name == "BillboardRenderer" ||
        sys.name == "PointLightSystem" || sys.name == "DirectionalLightSystem") {
        return true;
    }
    return std::ranges::any_of(sys.filter.entries,
                               [](const auto& entry) { return entry.qualified_name.rfind("std.", 0) == 0; });
}

bool is_render_phase_extern(const ExternSystemNode& sys, const DecoratedProgram& program) {
    (void)program;
    if (!uses_stdlib_extern_contract(sys)) {
        return false;
    }
    if (sys.name == "ShapeRenderer") {
        return filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.shapes.Shape", "Shape");
    }
    if (sys.name == "SpriteRenderer") {
        return filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.sprites.Renderer", "Renderer");
    }
    if (sys.name == "MeshRenderer") {
        return filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.meshes.Renderer", "Renderer");
    }
    if (sys.name == "BillboardRenderer") {
        return filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.meshes.BillboardRenderer", "BillboardRenderer");
    }
    if (sys.name == "PointLightSystem") {
        return filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.meshes.PointLight", "PointLight");
    }
    if (sys.name == "DirectionalLightSystem") {
        return filter_has_trait(sys.filter, "std.render.meshes.DirectionalLight", "DirectionalLight");
    }
    return false;
}

bool is_update_phase_extern(const ExternSystemNode& sys, const DecoratedProgram& program) {
    return !is_render_phase_extern(sys, program);
}

std::optional<std::string> raylib_key_constant(const ExprNode& expr) {
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        if (const auto* ident = std::get_if<IdentExpr>(&member->object->expr)) {
            if (ident->name == "Key") {
                return "KEY_" + upper_copy(member->member);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> raylib_mouse_constant(const ExprNode& expr) {
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        if (const auto* ident = std::get_if<IdentExpr>(&member->object->expr)) {
            if (ident->name == "MouseButton") {
                return "MOUSE_BUTTON_" + upper_copy(snake_case(member->member));
            }
        }
    }
    return std::nullopt;
}

std::string pad_to_width(const std::string& value, std::size_t width) {
    if (value.size() >= width) {
        return value;
    }
    return value + std::string(width - value.size(), ' ');
}
}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string CppEnttCodegen::generate(const DecoratedProgram& program) {
    std::ostringstream out;

    // Header
    out << "// Generated by Cactus DSL Compiler (cpp-entt backend)\n\n";
    out << "#include \"backends/cpp-entt/runtime.hpp\"\n";
    out << "\n";
    out << "#include <entt/entt.hpp>\n";
    out << "#include <raylib.h>\n";
    out << "\n";
    out << "#include <cstdint>\n";
    out << "#include <string>\n";
    out << "#include <unordered_set>\n";
    out << "#include <vector>\n";
    // Task 6.2: Include runtime header when extern funcs are present
    if (has_extern_funcs(program)) {
        out << "#include \"cactus_runtime.hpp\"\n";
    }
    out << "\n";

    out << "using Quat = cactus::runtime::Quat;\n";
    out << "\n";

    // Helper constructors for generated DSL built-ins
    out << "inline Vector2 vec2(float x, float y) {\n";
    out << "    return Vector2{.x = x, .y = y};\n";
    out << "}\n\n";

    out << "inline Vector3 vec3(float x, float y, float z) {\n";
    out << "    return Vector3{.x = x, .y = y, .z = z};\n";
    out << "}\n\n";

    out << "inline cactus::runtime::Quat quat(float x, float y, float z, float w) {\n";
    out << "    return cactus::runtime::Quat{.x = x, .y = y, .z = z, .w = w};\n";
    out << "}\n\n";

    // Built-in runtime helpers for generated examples
    out << "struct TickEvent {\n";
    out << "    float dt;\n";
    out << "};\n";
    out << "\n";

    if (program.ast != nullptr) {
        bool has_axis_input   = false;
        bool has_button_input = false;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                has_axis_input   = has_axis_input || input->input_kind == InputKind::Axis;
                has_button_input = has_button_input || input->input_kind == InputKind::Button;
            }
        }

        if (has_button_input) {
            out << "using InputButton = std::uint8_t;\n";
            std::uint8_t button_index = 0;
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Button) {
                        continue;
                    }
                    out << "constexpr InputButton " << input_action_constant_name(input->name)
                        << " = static_cast<InputButton>(" << static_cast<int>(button_index++) << ");\n";
                }
            }
            out << "\n";

            out << "namespace cactus::runtime::entt_backend {\n";
            out << "int cactus_input_button_key(std::uint8_t button) noexcept {\n";
            out << "    switch (button) {\n";
            button_index = 0;
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Button) {
                        continue;
                    }
                    std::string key = "0";
                    for (const auto& prop : input->props) {
                        if (prop.key == "key") {
                            if (auto maybe_key = raylib_key_constant(*prop.value)) {
                                key = *maybe_key;
                            } else if (auto maybe_mouse = raylib_mouse_constant(*prop.value)) {
                                key = *maybe_mouse;
                            }
                        }
                    }
                    out << "        case static_cast<InputButton>(" << static_cast<int>(button_index++) << "): return "
                        << key << ";\n";
                }
            }
            out << "    }\n";
            out << "    return 0;\n";
            out << "}  // namespace cactus::runtime::entt_backend\n\n";
        }

        if (has_axis_input) {
            out << "using InputAxis = std::uint8_t;\n";
            out << "enum class CactusInputAxisTag : std::uint8_t { ";
            bool first = true;
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Axis) {
                        continue;
                    }
                    out << (first ? "" : ", ") << input->name;
                    first = false;
                }
            }
            out << " };\n\n";

            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Axis) {
                        continue;
                    }
                    out << "constexpr InputAxis " << input_action_constant_name(input->name)
                        << " = static_cast<InputAxis>(CactusInputAxisTag::" << input->name << ");\n";
                }
            }
            out << "\n";

            out << "namespace cactus::runtime::entt_backend {\n";
            out << "float cactus_input_axis_value(std::uint8_t action) noexcept {\n";
            out << "    switch (action) {\n";
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Axis) {
                        continue;
                    }
                    std::string negative = "0";
                    std::string positive = "0";
                    for (const auto& prop : input->props) {
                        if (prop.key == "negative") {
                            if (auto key = raylib_key_constant(*prop.value)) {
                                negative = "(IsKeyDown(" + *key + ") ? 1.0F : 0.0F)";
                            }
                        } else if (prop.key == "positive") {
                            if (auto key = raylib_key_constant(*prop.value)) {
                                positive = "(IsKeyDown(" + *key + ") ? 1.0F : 0.0F)";
                            }
                        }
                    }
                    out << "        case static_cast<InputAxis>(CactusInputAxisTag::" << input->name << "):\n";
                    out << "            return " << positive << " - " << negative << ";\n";
                }
            }
            out << "        default:\n";
            out << "            return 0.0F;\n";
            out << "    }\n";
            out << "}\n\n";
            out << "}  // namespace cactus::runtime::entt_backend\n\n";
        }

        if (has_axis_input || has_button_input) {
            out << "struct InputEvent {\n";
            if (has_axis_input) {
                out << "    [[nodiscard]] static float axis(InputAxis action) {\n";
                out << "        return cactus::runtime::entt_backend::axis(action);\n";
                out << "    }\n";
            }
            out << "};\n\n";
        }
    }

    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            if (const auto* cb = std::get_if<ConstBlockNode>(&decl)) {
                for (const auto& ca : cb->assignments) {
                    if (ca.name == "WINDOW_WIDTH" || ca.name == "WINDOW_HEIGHT" || ca.name == "WINDOW_TITLE" ||
                        ca.name == "TARGET_FPS") {
                        continue;
                    }
                    out << "[[maybe_unused]] constexpr auto " << upper_copy(ca.name) << " = "
                        << ManualSystemEmitter::emit_expr(*ca.value, program.ast) << ";\n";
                }
            }
        }
        out << "\n";
    }

    if (program.ast != nullptr) {
        std::uint32_t next_asset_handle = 1U;
        bool emitted_asset_constants    = false;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* asset = std::get_if<AssetDeclNode>(&decl)) {
                if (!emitted_asset_constants) {
                    out << "// ── Asset Handles ──────────────────────────────────────────────────\n\n";
                    emitted_asset_constants = true;
                }
                out << "constexpr cactus::runtime::AssetHandle " << asset->name << " = " << next_asset_handle++
                    << "U; // NOLINT(readability-identifier-naming)\n";
            }
        }
        if (emitted_asset_constants) {
            out << "\n";
        }
    }

    // Enums
    for (const auto& [name, e] : program.enums) {
        auto emitted_code = EnttComponentEmitter::emit_enum(e);
        if (!emitted_code.empty()) {
            out << emitted_code << "\n";
        }
    }

    // POD structs
    for (const auto& [name, s] : program.structs) {
        out << EnttComponentEmitter::emit_pod_struct(s) << "\n";
    }

    // Component structs (from traits)
    for (const auto& [name, t] : program.traits) {
        out << EnttComponentEmitter::emit_component(t) << "\n";
    }

    // Events
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* event = std::get_if<EventNode>(&decl)) {
                out << EnttEventEmitter::emit_event(*event, program) << "\n";
            }
        }
    }

    out << EnttSystemEmitter::emit_entt_hierarchy_helpers(program);

    // Persist serialization stubs
    out << "// ── Persist Serialization ────────────────────────────────────────────\n\n";
    for (const auto& [name, t] : program.traits) {
        bool has_persist = false;
        for (const auto& f : t.fields) {
            if (f.is_persist) {
                has_persist = true;
                break;
            }
        }
        if (has_persist) {
            out << "void save_" << name << "(const " << name << "& comp) {\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // serialize comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
            out << "void load_" << name << "(" << name << "& comp) {\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // deserialize into comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
        }
    }

    // Sync replication stubs
    out << "// ── Sync Replication ─────────────────────────────────────────────────\n\n";
    for (const auto& [name, t] : program.traits) {
        bool has_sync = false;
        for (const auto& f : t.fields) {
            if (f.is_sync) {
                has_sync = true;
                break;
            }
        }
        if (has_sync) {
            out << "void replicate_" << name << "(const " << name << "& comp) {\n";
            for (const auto& f : t.fields) {
                if (f.is_sync) {
                    out << "    // send delta for comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
        }
    }

    // System functions
    if (program.ast != nullptr) {
        out << "// ── Systems ─────────────────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<SystemNode>(&decl)) {
                out << EnttSystemEmitter::emit_system(*sys, program);
            }
            if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
                out << EnttSystemEmitter::emit_extern_system(*sys, program);
            }
        }
    }

    // Entity creation from units
    if (program.ast != nullptr) {
        out << "// ── Entity Creation ─────────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* unit = std::get_if<UnitNode>(&decl)) {
                out << "entt::entity create_" << snake_case(unit->name) << "(entt::registry& registry) {\n";
                out << "    auto entity = registry.create();\n";
                for (const auto& trait : unit->traits) {
                    out << "    {\n";
                    std::size_t widest = std::string("auto component").size();
                    for (const auto& assignment : trait.assignments) {
                        widest = std::max(widest, std::string("component.").size() + assignment.name.size());
                    }
                    out << "        " << pad_to_width("auto component", widest) << " = " << trait.trait_name << "{};\n";
                    for (const auto& assignment : trait.assignments) {
                        out << "        " << pad_to_width("component." + assignment.name, widest) << " = "
                            << ManualSystemEmitter::emit_expr(*assignment.value, program.ast) << ";\n";
                    }
                    out << "        registry.emplace<" << trait.trait_name << ">(entity, component);\n";
                    out << "    }\n";
                }
                out << "    return entity;\n";
                out << "}\n\n";
            }
        }
    }

    // Dispatcher setup
    out << "// ── Event Dispatcher ────────────────────────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "void generated_setup_dispatcher(entt::dispatcher& dispatcher) {\n";
    if (program.ast != nullptr) {
        out << "    (void)dispatcher;\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* event = std::get_if<EventNode>(&decl)) {
                out << "    " << EnttEventEmitter::emit_sink_connection(*event);
            }
        }
    }
    out << "}\n\n";

    // Main game loop — extract window constants from const block if available
    std::string win_width  = "800";
    std::string win_height = "600";
    std::string win_title  = "\"Cactus Game\"";
    std::string win_fps    = "60";
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* cb = std::get_if<ConstBlockNode>(&decl)) {
                for (auto& ca : cb->assignments) {
                    if (ca.name == "WINDOW_WIDTH") {
                        win_width = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "WINDOW_HEIGHT") {
                        win_height = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "WINDOW_TITLE") {
                        win_title = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "TARGET_FPS") {
                        win_fps = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
                    }
                }
            }
        }
    }

    out << "// ── Runtime Glue ────────────────────────────────────────────────────\n\n";
    out << "ProjectConfig generated_project_config() noexcept {\n";
    out << "    return {.window_width = " << win_width << ", .window_height = " << win_height
        << ", .window_title = " << win_title << ", .target_fps = " << win_fps << "};\n";
    out << "}\n\n";

    out << "void generated_init_project(entt::registry& registry) {\n";
    out << "    (void)registry;\n";
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* asset = std::get_if<AssetDeclNode>(&decl)) {
                const auto registration = asset_register_call(*asset);
                if (!registration.empty()) {
                    out << "    " << registration << "\n";
                }
            }
        }
        for (auto& decl : program.ast->declarations) {
            if (auto* unit = std::get_if<UnitNode>(&decl)) {
                out << "    create_" << snake_case(unit->name) << "(registry);\n";
            }
        }
    }
    out << "}\n\n";

    out << "void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt) {\n";

    out << "    (void)dt;\n\n";

    // Call system input handlers
    if (program.ast != nullptr) {
        bool emits_input_handler = false;
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<SystemNode>(&decl)) {
                for (auto& handler : sys->handlers) {
                    if (handler.event_name == "input") {
                        emits_input_handler = true;
                        break;
                    }
                }
            }
            if (emits_input_handler) {
                break;
            }
        }
        if (emits_input_handler) {
            out << "    auto input = InputEvent{};\n";
            for (auto& decl : program.ast->declarations) {
                if (auto* sys = std::get_if<SystemNode>(&decl)) {
                    for (auto& handler : sys->handlers) {
                        if (handler.event_name == "input") {
                            out << "    " << system_function_name(sys->name, "input") << "(registry, input);\n";
                        }
                    }
                }
            }
            out << "\n";
        }
    }

    // Call non-render system tick handlers
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<SystemNode>(&decl)) {
                for (auto& handler : sys->handlers) {
                    if (handler.event_name == "tick") {
                        out << "    " << system_function_name(sys->name, "tick") << "(registry, TickEvent{dt});\n";
                    }
                }
            }
            if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
                if (!is_update_phase_extern(*sys, program)) {
                    continue;
                }
                out << "    " << system_function_name(sys->name, "tick") << "(registry);\n";
            }
        }
    }

    out << "    dispatcher.update();\n";
    out << "}\n\n";

    out << "void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher) {\n";
    out << "    (void)registry;\n";
    out << "    (void)dispatcher;\n";
    out << "    cactus::runtime::entt_backend::begin_render_frame();\n";
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
                if (is_render_phase_extern(*sys, program)) {
                    out << "    " << system_function_name(sys->name, "tick") << "(registry);\n";
                }
            }
        }
    }
    out << "    cactus::runtime::entt_backend::end_render_frame();\n";
    out << "}\n";
    out << "\n}  // namespace cactus::runtime::entt_backend\n";

    return out.str();
}

}  // namespace cactus
