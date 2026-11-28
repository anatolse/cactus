#include "backends/cpp-entt/drawing_rule_emitter.hpp"

#include "backends/cpp-entt/drawing_rule_glsl_helpers.hpp"
#include "backends/cpp-entt/system_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"
#include "common/ast_expressions.hpp"
#include "common/codegen_error.hpp"
#include "common/color_channels.hpp"
#include "common/drawing_rule_builtin_fields.hpp"
#include "common/drawing_rule_intrinsics.hpp"
#include "common/joined.hpp"
#include "common/vector_binary_ops.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

namespace {

// ── GLSL value shapes ──────────────────────────────────────────────────────

enum class GlslType : std::uint8_t { Float, Int, Bool, Vec2, Vec3, Color, Texture, Struct, Void };

TypeKind glsl_type_to_type_kind(GlslType type) {
    switch (type) {
        case GlslType::Float:
            return TypeKind::Float;
        case GlslType::Int:
            return TypeKind::Int;
        case GlslType::Bool:
            return TypeKind::Bool;
        case GlslType::Vec2:
            return TypeKind::Vec2;
        case GlslType::Vec3:
            return TypeKind::Vec3;
        case GlslType::Color:
            return TypeKind::Color;
        case GlslType::Texture:
            return TypeKind::TextureId;
        case GlslType::Struct:
            return TypeKind::Struct;
        case GlslType::Void:
            return TypeKind::Void;
    }
    std::unreachable();
}

GlslType type_kind_to_glsl_type(TypeKind kind) {
    switch (kind) {
        case TypeKind::Float:
            return GlslType::Float;
        case TypeKind::Int:
        case TypeKind::Enum:
            return GlslType::Int;
        case TypeKind::Bool:
            return GlslType::Bool;
        case TypeKind::Vec2:
            return GlslType::Vec2;
        case TypeKind::Vec3:
            return GlslType::Vec3;
        case TypeKind::Color:
            return GlslType::Color;
        case TypeKind::TextureId:
            return GlslType::Texture;
        default:
            throw std::runtime_error("drawing-rule GLSL codegen: a value of this type has no GLSL representation");
    }
}

// A module-qualified name as one GLSL identifier.
std::string glsl_identifier(const std::string& canonical_id) {
    std::string name = canonical_id;
    std::ranges::replace(name, '.', '_');
    return name;
}

// The text, shape and, for a struct, the declaration of a translated value.
struct TranslatedExpr {
    std::string text;
    GlslType type{};
    const ResolvedStruct* structure = nullptr;
};

std::string glsl_type_name(const TranslatedExpr& shape) {
    switch (shape.type) {
        case GlslType::Float:
            return "float";
        case GlslType::Int:
            return "int";
        case GlslType::Bool:
            return "bool";
        case GlslType::Vec2:
            return "vec2";
        case GlslType::Vec3:
            return "vec3";
        case GlslType::Color:
            return "vec4";
        case GlslType::Texture:
            return "sampler2D";
        case GlslType::Struct:
            return glsl_identifier(shape.structure->canonical_id);
        case GlslType::Void:
            return "void";
    }
    std::unreachable();
}

// A world value a stage reads, set as a uniform once per draw or once per row.
struct StageUniform {
    std::string glsl_name;
    std::string key;  // what it reads, so two reads of one value share a uniform
    GlslType type{};
    TypeKind source{};  // an enum value is uploaded as its index
    bool per_row = true;
    std::string component_path;      // a field of the row's own trait
    const ExprNode* read = nullptr;  // any other read, lowered by the system emitter
};

struct FilterTrait {
    SymbolId symbol;
    std::string component_local;  // as the domain walk binds it
};

// The helpers, structs and functions one shader needs, each emitted once, before its first use.
struct ShaderLibrary {
    std::unordered_set<std::string> helpers;
    std::unordered_set<std::string> declared;  // struct and function names already emitted
    std::string structs;
    std::string functions;
};

struct GlslCtx {
    const DecoratedProgram* program = nullptr;
    ShaderLibrary* library          = nullptr;
    std::string alias;  // the stage handler's own alias ("v" / "f"); empty in a function
    std::unordered_map<std::string, FilterTrait> filter_traits;
    // Built-in fields, authored locals and parameters, by Cactus name. Authored names
    // are emitted as `let_<name>` so no name meets a GLSL reserved word (`half`).
    std::unordered_map<std::string, TranslatedExpr> locals;
    std::vector<StageUniform>* uniforms = nullptr;  // shared by both stages of one rule
    std::vector<std::string> used_uniforms;         // the ones this stage declares
    TranslatedExpr result;                          // a function's result shape
};

TranslatedExpr shape_of(const TypeInfo& type, const DecoratedProgram& program) {
    if (type.kind != TypeKind::Struct) {
        return {.type = type_kind_to_glsl_type(type.kind)};
    }
    const auto* structure =
        type.symbol_id.has_value() ? EnttCodegenUtils::find_struct(program, *type.symbol_id) : nullptr;
    if (structure == nullptr) {
        throw std::runtime_error("drawing-rule GLSL codegen: unknown struct '" + type.name + "'");
    }
    return {.type = GlslType::Struct, .structure = structure};
}

TranslatedExpr return_shape(const ResolvedFunc& func, const DecoratedProgram& program) {
    return func.return_type.has_value() ? shape_of(*func.return_type, program) : TranslatedExpr{.type = GlslType::Void};
}

// `value` where a `target` value is expected: an `int` converts to `float` as on the CPU.
std::string converted(const TranslatedExpr& value, GlslType target) {
    return value.type == GlslType::Int && target == GlslType::Float ? "float(" + value.text + ")" : value.text;
}

bool is_float_based(GlslType type) {
    return type == GlslType::Float || type == GlslType::Vec2 || type == GlslType::Vec3 || type == GlslType::Color;
}

TranslatedExpr use_helper(std::string_view helper, std::string text, TranslatedExpr shape, GlslCtx& ctx) {
    ctx.library->helpers.emplace(helper);
    shape.text = std::move(text);
    return shape;
}

std::string hex_color_literal_to_glsl(const std::string& raw_hex) {
    if (raw_hex.size() != 6 && raw_hex.size() != 8) {
        throw std::runtime_error("drawing-rule GLSL codegen: malformed hex color literal '#" + raw_hex + "'");
    }
    const auto bytes   = hex_color_bytes(raw_hex);
    const auto channel = [&](std::size_t index) {
        // Enough digits that GLSL reads back the exact float a rule uses for this literal.
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<float>::max_digits10) << color_channel_from_byte(bytes[index]);
        return out.str();
    };
    return "vec4(" + channel(0) + ", " + channel(1) + ", " + channel(2) + ", " + channel(3) + ")";
}

// Declares `uniform` once for the rule and once for each stage that reads it.
TranslatedExpr use_uniform(StageUniform uniform, GlslCtx& ctx) {
    if (ctx.uniforms == nullptr) {
        throw std::runtime_error("drawing-rule GLSL codegen: a function reads only its parameters");
    }
    auto& all = *ctx.uniforms;
    auto found =
        std::ranges::find_if(all, [&](const StageUniform& existing) { return existing.key == uniform.key; });
    if (found == all.end()) {
        if (uniform.glsl_name.empty()) {
            uniform.glsl_name = "u_read_" + std::to_string(all.size());
        }
        all.push_back(std::move(uniform));
        found = std::prev(all.end());
    }
    if (!std::ranges::contains(ctx.used_uniforms, found->glsl_name)) {
        ctx.used_uniforms.push_back(found->glsl_name);
    }
    return {.text = found->glsl_name, .type = found->type};
}

TranslatedExpr translate_expr(const ExprNode& expr, GlslCtx& ctx);
std::string translate_stmts(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx, int indent);

std::vector<TranslatedExpr> translate_args(const std::vector<std::unique_ptr<ExprNode>>& args, GlslCtx& ctx) {
    std::vector<TranslatedExpr> translated;
    translated.reserve(args.size());
    for (const auto& arg : args) {
        translated.push_back(translate_expr(*arg, ctx));
    }
    return translated;
}

std::string joined_args(const std::vector<TranslatedExpr>& args, const std::vector<GlslType>& targets) {
    std::string joined;
    for (std::size_t index = 0; index < args.size(); ++index) {
        joined += (index == 0 ? "" : ", ") + converted(args[index], index < targets.size() ? targets[index] : args[index].type);
    }
    return joined;
}

// ── Structs and functions ──────────────────────────────────────────────────

std::string field_name(const std::string& name) {
    return "f_" + name;
}

std::string local_name(const std::string& name) {
    return "let_" + name;
}

void ensure_struct(const ResolvedStruct& structure, GlslCtx& ctx) {
    const auto name = glsl_identifier(structure.canonical_id);
    if (ctx.library->declared.contains(name)) {
        return;
    }
    std::string text = "struct " + name + " {\n";
    for (const auto& field : structure.fields) {
        const auto shape = shape_of(field.type, *ctx.program);
        if (shape.structure != nullptr) {
            ensure_struct(*shape.structure, ctx);
        }
        text += "    " + glsl_type_name(shape) + " " + field_name(field.name) + ";\n";
    }
    text += "};\n\n";
    ctx.library->structs += text;
    ctx.library->declared.insert(name);
}

[[noreturn]] void throw_no_translation(const SymbolId& symbol) {
    throw std::runtime_error("drawing-rule GLSL codegen: '" + make_canonical_id(symbol) +
                             "' has no registered portable GLSL translation");
}

// Emits `func` as shader function `name` after the functions it calls.
void ensure_function(const std::string& name, const SymbolId& symbol, const ResolvedFunc& func, GlslCtx& caller) {
    if (caller.library->declared.contains(name)) {
        return;
    }
    const auto* node = find_func_decl(*caller.program->ast, symbol);
    if (node == nullptr) {
        throw_no_translation(symbol);
    }
    GlslCtx ctx{.program = caller.program, .library = caller.library, .result = return_shape(func, *caller.program)};
    std::string params;
    for (const auto& param : func.params) {
        auto shape = shape_of(param.type, *ctx.program);
        shape.text = local_name(param.name);
        params += (params.empty() ? "" : ", ") + glsl_type_name(shape) + " " + shape.text;
        ctx.locals[param.name] = shape;
    }
    if (ctx.result.structure != nullptr) {
        ensure_struct(*ctx.result.structure, ctx);
    }
    const auto body = translate_stmts(node->body, ctx, 1);
    caller.library->functions += glsl_type_name(ctx.result) + " " + name + "(" + params + ")\n{\n" + body + "}\n\n";
    caller.library->declared.insert(name);
}

// ── Reads ──────────────────────────────────────────────────────────────────

const ResolvedField* find_trait_field(const ResolvedTrait* trait, const std::string& name) {
    return trait == nullptr ? nullptr : find_field(trait->fields, name);
}

TranslatedExpr translate_builtin_field_access(const std::string& field_name, const GlslCtx& ctx) {
    const auto found = ctx.locals.find(field_name);
    if (found == ctx.locals.end()) {
        throw std::runtime_error("drawing-rule GLSL codegen: '" + ctx.alias + "." + field_name +
                                 "' is not a built-in field here");
    }
    return found->second;
}

// `alias.field`: a field of the row's own trait, set once per row.
TranslatedExpr translate_row_field(const FilterTrait& filter_trait,
                                   const std::string& owner_name,
                                   const std::string& field_name,
                                   GlslCtx& ctx) {
    const auto* trait = EnttCodegenUtils::find_trait(*ctx.program, make_canonical_id(filter_trait.symbol));
    const auto* field = find_trait_field(trait, field_name);
    if (field == nullptr) {
        throw std::runtime_error("drawing-rule GLSL codegen: unknown trait field '" + owner_name + "." + field_name +
                                 "'");
    }
    const auto name = "u_" + owner_name + "_" + field_name;
    return use_uniform(StageUniform{.glsl_name      = name,
                                    .key            = name,
                                    .type           = shape_of(field->type, *ctx.program).type,
                                    .source         = field->type.kind,
                                    .per_row        = true,
                                    .component_path = filter_trait.component_local + "." + field->name},
                       ctx);
}

// `X[T].field` of a named entity, set once per draw, or `row.link[T].field`, set once per row.
TranslatedExpr translate_trait_read(const ExprNode& read,
                                    const TraitAccessExpr& access,
                                    const std::string& field_name,
                                    GlslCtx& ctx) {
    const auto& trait_id = access.link_target.has_value() ? access.link_target : access.resolved_trait_id;
    const auto* trait = trait_id.has_value() ? EnttCodegenUtils::find_trait(*ctx.program, make_canonical_id(*trait_id))
                                             : nullptr;
    const auto* field = find_trait_field(trait, field_name);
    if (field == nullptr || access.cell) {
        throw std::runtime_error("drawing-rule GLSL codegen: unsupported read '" + expression_spelling(read, "?") + "'");
    }
    const bool per_row = !access.named_ref().has_value();
    return use_uniform(StageUniform{.key     = expression_spelling(read, "?") + (per_row ? "#row" : "#draw"),
                                    .type    = shape_of(field->type, *ctx.program).type,
                                    .source  = field->type.kind,
                                    .per_row = per_row,
                                    .read    = &read},
                       ctx);
}

bool is_component_access(GlslType object_type, const std::string& member) {
    switch (object_type) {
        case GlslType::Vec2:
            return member == "x" || member == "y";
        case GlslType::Vec3:
            return member == "x" || member == "y" || member == "z";
        case GlslType::Color:
            return member == "r" || member == "g" || member == "b" || member == "a";
        default:
            return false;
    }
}

// A field of a struct value, or a component of a vector or color.
TranslatedExpr translate_value_member(const TranslatedExpr& object, const std::string& member, const GlslCtx& ctx) {
    if (object.type == GlslType::Struct) {
        const auto* field = find_field(object.structure->fields, member);
        if (field == nullptr) {
            throw std::runtime_error("drawing-rule GLSL codegen: unknown struct field '." + member + "'");
        }
        auto shape = shape_of(field->type, *ctx.program);
        shape.text = object.text + "." + field_name(member);
        return shape;
    }
    if (!is_component_access(object.type, member)) {
        throw std::runtime_error("drawing-rule GLSL codegen: unsupported member access '." + member + "'");
    }
    return {.text = object.text + "." + member, .type = GlslType::Float};
}

// A texture asset, bound once per draw.
TranslatedExpr translate_asset_read(const ExprNode& read, GlslCtx& ctx) {
    return use_uniform(StageUniform{.key     = expression_spelling(read, "?") + "#asset",
                                    .type    = GlslType::Texture,
                                    .source  = TypeKind::TextureId,
                                    .per_row = false,
                                    .read    = &read},
                       ctx);
}

bool is_texture_asset(const std::string& name, const GlslCtx& ctx) {
    return std::ranges::any_of(ctx.program->ast->declarations, [&](const auto& decl) {
        const auto* asset = std::get_if<AssetDeclNode>(&decl);
        return asset != nullptr && asset->name == name && asset->asset_kind == AssetKind::Texture;
    });
}

const ExprNode* const_initializer(const std::optional<SymbolId>& constant, const GlslCtx& ctx) {
    return constant.has_value() ? EnttCodegenUtils::find_const_initializer(*ctx.program->ast, *constant) : nullptr;
}

TranslatedExpr translate_member(const MemberExpr& member, const ExprNode& node, GlslCtx& ctx) {
    if (const auto* initializer = const_initializer(member.resolved_const_id, ctx)) {
        return translate_expr(*initializer, ctx);
    }
    if (member.resolved_enum_member.has_value()) {
        return {.text = std::to_string(member.resolved_enum_member->index), .type = GlslType::Int};
    }
    if (member.resolved_asset_type == TypeKind::TextureId) {
        return translate_asset_read(node, ctx);
    }
    if (const auto* access = std::get_if<TraitAccessExpr>(&member.object->expr)) {
        return translate_trait_read(node, *access, member.member, ctx);
    }
    const auto* owner     = std::get_if<IdentExpr>(&member.object->expr);
    const bool stage_name = owner != nullptr && !owner->resolved_const_id.has_value() && !ctx.locals.contains(owner->name);
    if (owner != nullptr && !ctx.alias.empty() && owner->name == ctx.alias) {
        return translate_builtin_field_access(member.member, ctx);
    }
    if (const auto trait_it = stage_name ? ctx.filter_traits.find(owner->name) : ctx.filter_traits.end();
        trait_it != ctx.filter_traits.end()) {
        return translate_row_field(trait_it->second, owner->name, member.member, ctx);
    }
    if (stage_name) {
        throw std::runtime_error("drawing-rule GLSL codegen: unresolved member access '" + owner->name + "." +
                                 member.member + "'");
    }
    return translate_value_member(translate_expr(*member.object, ctx), member.member, ctx);
}

// ── Calls ──────────────────────────────────────────────────────────────────

const TranslatedExpr kFloat{.type = GlslType::Float};
const TranslatedExpr kInt{.type = GlslType::Int};
const TranslatedExpr kVec2{.type = GlslType::Vec2};
const TranslatedExpr kVec3{.type = GlslType::Vec3};

// A `std.math` extern: a GLSL built-in where it gives the CPU's result, else a helper that does.
TranslatedExpr translate_math_extern(std::string_view name, const std::vector<TranslatedExpr>& args, GlslCtx& ctx) {
    std::vector<GlslType> floats(args.size(), GlslType::Float);
    const auto call = [&](std::string_view function) {
        return std::string(function) + "(" + joined_args(args, floats) + ")";
    };
    if (name == "abs" || name == "radians" || name == "degrees") {
        return {.text = call(name), .type = GlslType::Float};
    }
    if (name == "floor" || name == "ceil") {
        return use_helper("to_int", "cactus_to_int(" + call(name) + ")", kInt, ctx);
    }
    if (name == "round") {
        return use_helper("round", call("cactus_round"), kInt, ctx);
    }
    return use_helper(name, call("cactus_" + std::string(name)), kFloat, ctx);
}

TranslatedExpr translate_vector_extern(std::string_view name,
                                       bool vec3,
                                       const std::vector<TranslatedExpr>& args,
                                       GlslCtx& ctx) {
    const auto vector = vec3 ? GlslType::Vec3 : GlslType::Vec2;
    std::vector<GlslType> targets;
    targets.reserve(args.size());
    for (const auto& arg : args) {
        targets.push_back(arg.type == GlslType::Int ? GlslType::Float : arg.type);
    }
    const auto call = [&](std::string_view function) {
        return std::string(function) + "(" + joined_args(args, targets) + ")";
    };
    if (name == "length" || name == "dot" || name == "distance") {
        return {.text = call(name), .type = GlslType::Float};
    }
    if (name == "cross" || name == "reflect") {
        return {.text = call(name), .type = vector};
    }
    if (name == "angle") {
        return use_helper("atan2", "cactus_atan2(" + args[0].text + ".y, " + args[0].text + ".x)", kFloat, ctx);
    }
    if (name != "lerp" && name != "clamp" && name != "normalize" && name != "rotate") {
        throw std::runtime_error("drawing-rule GLSL codegen: no translation for vector function '" + std::string(name) +
                                 "'");
    }
    // lerp and clamp are GLSL overloads of the scalar helpers; normalize and rotate name their size.
    const std::string suffix   = vec3 ? "3" : "2";
    const bool overloaded      = name == "lerp" || name == "clamp";
    const std::string function = "cactus_" + std::string(name) + (overloaded ? "" : suffix);
    return use_helper(std::string(name) + suffix, call(function), vec3 ? kVec3 : kVec2, ctx);
}

TranslatedExpr translate_struct_construction(const CallExpr& call, const SymbolId& symbol, GlslCtx& ctx) {
    const auto* structure = EnttCodegenUtils::find_struct(*ctx.program, symbol);
    if (structure == nullptr) {
        throw std::runtime_error("drawing-rule GLSL codegen: unknown struct '" + make_canonical_id(symbol) + "'");
    }
    ensure_struct(*structure, ctx);
    std::vector<std::string> args;
    for (std::size_t index = 0; index < structure->fields.size(); ++index) {
        const auto& field = structure->fields[index];
        auto position     = index;
        if (call.has_named_args()) {
            const auto named = std::ranges::find(call.arg_names, field.name);
            if (named == call.arg_names.end()) {
                throw std::runtime_error("drawing-rule GLSL codegen: struct field '" + field.name + "' has no value");
            }
            position = static_cast<std::size_t>(named - call.arg_names.begin());
        }
        args.push_back(converted(translate_expr(*call.args.at(position), ctx), shape_of(field.type, *ctx.program).type));
    }
    TranslatedExpr result{.type = GlslType::Struct, .structure = structure};
    result.text = glsl_type_name(result) + "(" + joined(args, ", ") + ")";
    return result;
}

TranslatedExpr translate_func_call(const CallExpr& call, const SymbolId& callee, GlslCtx& ctx) {
    const auto* func = EnttCodegenUtils::find_func(*ctx.program, callee);
    if (func == nullptr) {
        throw_no_translation(callee);
    }
    const auto name = glsl_identifier(make_canonical_id(callee));
    ensure_function(name, callee, *func, ctx);
    std::vector<GlslType> targets;
    targets.reserve(func->params.size());
    for (const auto& param : func->params) {
        targets.push_back(shape_of(param.type, *ctx.program).type);
    }
    auto result = return_shape(*func, *ctx.program);
    result.text = name + "(" + joined_args(translate_args(call.args, ctx), targets) + ")";
    return result;
}

// `vec2(...)`, `vec3(...)` or `color(...)`.
std::optional<TranslatedExpr> constructor_shape(const std::string& name) {
    if (name == "vec2") {
        return TranslatedExpr{.text = "vec2", .type = GlslType::Vec2};
    }
    if (name == "vec3") {
        return TranslatedExpr{.text = "vec3", .type = GlslType::Vec3};
    }
    if (name == "color") {
        return TranslatedExpr{.text = "vec4", .type = GlslType::Color};
    }
    return std::nullopt;
}

TranslatedExpr translate_call(const CallExpr& call, GlslCtx& ctx) {
    const auto* callee_ident = std::get_if<IdentExpr>(&call.callee->expr);
    if (auto shape = callee_ident != nullptr ? constructor_shape(callee_ident->name) : std::nullopt) {
        const auto args = translate_args(call.args, ctx);
        shape->text += "(" + joined_args(args, std::vector(args.size(), GlslType::Float)) + ")";
        return *shape;
    }
    if (call.resolved_struct_id.has_value()) {
        return translate_struct_construction(call, *call.resolved_struct_id, ctx);
    }
    if (!call.resolved_callee_id.has_value()) {
        throw std::runtime_error("drawing-rule GLSL codegen: unresolved call in stage handler body");
    }
    const auto& callee = *call.resolved_callee_id;
    if (is_portable_glsl_intrinsic(callee)) {
        const auto args    = translate_args(call.args, ctx);
        const auto& module = callee.module.name;
        if (module == kTexturesModule) {
            return use_helper("sample",
                              "cactus_sample(" + joined_args(args, {GlslType::Texture, GlslType::Vec2}) + ")",
                              TranslatedExpr{.type = GlslType::Color},
                              ctx);
        }
        return module == "std.math" ? translate_math_extern(callee.local_name, args, ctx)
                                    : translate_vector_extern(callee.local_name, module == "std.math.vec3", args, ctx);
    }
    return translate_func_call(call, callee, ctx);
}

// ── Operators ──────────────────────────────────────────────────────────────

TranslatedExpr translate_literal(const LiteralExpr& literal) {
    switch (literal.kind) {
        case LiteralExpr::Kind::Int:
            return {.text = literal.value, .type = GlslType::Int};
        case LiteralExpr::Kind::Float:
            return {.text = literal.value, .type = GlslType::Float};
        case LiteralExpr::Kind::HexColor:
            return {.text = hex_color_literal_to_glsl(literal.value), .type = GlslType::Color};
        case LiteralExpr::Kind::Bool:
            return {.text = literal.value, .type = GlslType::Bool};
        case LiteralExpr::Kind::String:
        case LiteralExpr::Kind::Duration:
            throw std::runtime_error("drawing-rule GLSL codegen: unsupported literal in stage handler body");
    }
    std::unreachable();
}

TranslatedExpr translate_ident(const IdentExpr& ident, const ExprNode& node, GlslCtx& ctx) {
    if (const auto local_it = ctx.locals.find(ident.name); local_it != ctx.locals.end()) {
        return local_it->second;
    }
    if (const auto* initializer = const_initializer(ident.resolved_const_id, ctx)) {
        return translate_expr(*initializer, ctx);
    }
    if (is_texture_asset(ident.name, ctx)) {
        return translate_asset_read(node, ctx);
    }
    throw std::runtime_error("drawing-rule GLSL codegen: unresolved identifier '" + ident.name + "'");
}

// An `int` operand of a float-based operation converts to `float` first, as on the CPU.
void unify_numeric(TranslatedExpr& left, TranslatedExpr& right) {
    if (left.type == GlslType::Int && is_float_based(right.type)) {
        left = {.text = converted(left, GlslType::Float), .type = GlslType::Float};
    } else if (right.type == GlslType::Int && is_float_based(left.type)) {
        right = {.text = converted(right, GlslType::Float), .type = GlslType::Float};
    }
}

const char* int_division_helper(ArithmeticOperands operands, std::string_view op) {
    if (operands != ArithmeticOperands::Int || op.empty() || (op.front() != '/' && op.front() != '%')) {
        return nullptr;
    }
    return op.front() == '/' ? "cactus_div" : "cactus_mod";
}

TranslatedExpr translate_binary(const BinaryExpr& binary, GlslCtx& ctx) {
    auto left  = translate_expr(*binary.left, ctx);
    auto right = translate_expr(*binary.right, ctx);
    if (const char* helper = int_division_helper(binary.operands, binary.op)) {
        return use_helper(helper, std::string(helper) + "(" + left.text + ", " + right.text + ")", kInt, ctx);
    }
    if (binary.op == "and" || binary.op == "or") {
        return {.text = "(" + left.text + (binary.op == "and" ? " && " : " || ") + right.text + ")",
                .type = GlslType::Bool};
    }
    unify_numeric(left, right);
    const bool is_comparison = binary.op == "<" || binary.op == ">" || binary.op == "<=" || binary.op == ">=" ||
                               binary.op == "==" || binary.op == "!=";
    if (left.type != right.type && is_comparison) {
        throw std::runtime_error("drawing-rule GLSL codegen: cannot compare values of different types");
    }
    auto result_type = is_comparison ? GlslType::Bool : left.type;
    if (left.type != right.type) {
        const auto result_kind = lookup_vector_binary_op_result(
            glsl_type_to_type_kind(left.type), binary.op, glsl_type_to_type_kind(right.type));
        if (!result_kind.has_value()) {
            throw std::runtime_error("drawing-rule GLSL codegen: no operator '" + binary.op + "' for these operand types");
        }
        result_type = type_kind_to_glsl_type(*result_kind);
    }
    return {.text = "(" + left.text + " " + binary.op + " " + right.text + ")", .type = result_type};
}

// Two branches of one value: an `int` branch converts when the other is a `float`.
GlslType branch_type(const std::vector<TranslatedExpr>& branches) {
    const bool any_float = std::ranges::any_of(branches, [](const auto& b) { return b.type == GlslType::Float; });
    return any_float ? GlslType::Float : branches.front().type;
}

TranslatedExpr translate_if_expr(const IfExpr& node, GlslCtx& ctx) {
    const auto condition = translate_expr(*node.condition, ctx);
    const std::vector branches{translate_expr(*node.then_expr, ctx), translate_expr(*node.else_expr, ctx)};
    auto result = branches.front();
    result.type = branch_type(branches);
    result.text = "(" + condition.text + " ? " + converted(branches[0], result.type) + " : " +
                  converted(branches[1], result.type) + ")";
    return result;
}

// Stage values are pure, so the chain may repeat the subject.
TranslatedExpr translate_match_expr(const MatchExpr& match_expr, GlslCtx& ctx) {
    const auto subject = translate_expr(*match_expr.subject, ctx).text;
    std::vector<TranslatedExpr> bodies;
    std::vector<std::string> patterns;
    for (const auto& arm : match_expr.arms) {
        bodies.push_back(translate_expr(*arm.body, ctx));
        patterns.push_back(is_wildcard_pattern(*arm.pattern) ? "" : translate_expr(*arm.pattern, ctx).text);
    }
    auto result = bodies.back();
    result.type = branch_type(bodies);
    std::vector<std::pair<std::string, std::string>> tested_arms;
    for (std::size_t index = 0; index + 1 < bodies.size(); ++index) {
        tested_arms.emplace_back(patterns[index], converted(bodies[index], result.type));
    }
    result.text = EnttCodegenUtils::match_ternary_chain(subject, tested_arms, converted(bodies.back(), result.type), false);
    return result;
}

TranslatedExpr translate_unary(const UnaryExpr& unary, GlslCtx& ctx) {
    auto operand = translate_expr(*unary.operand, ctx);
    operand.text = "(" + std::string(unary.op == "not" ? "!" : unary.op) + operand.text + ")";
    return operand;
}

TranslatedExpr translate_expr_node(const ExprNode& expr, GlslCtx& ctx) {
    return std::visit(
        [&](const auto& node) -> TranslatedExpr {
            using E = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                return translate_literal(node);
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return translate_ident(node, expr, ctx);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return translate_unary(node, ctx);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return translate_binary(node, ctx);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                return translate_member(node, expr, ctx);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                return translate_call(node, ctx);
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                return translate_if_expr(node, ctx);
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                return translate_match_expr(node, ctx);
            } else {
                throw CodegenError(expr.location,
                                   "drawing-rule GLSL codegen: unsupported expression form in stage handler body");
            }
        },
        expr.expr);
}

// The innermost expression that fails gives the diagnostic its location.
TranslatedExpr translate_expr(const ExprNode& expr, GlslCtx& ctx) {
    try {
        return translate_expr_node(expr, ctx);
    } catch (const CodegenError&) {
        throw;
    } catch (const std::runtime_error& error) {
        throw CodegenError(expr.location, error.what());
    }
}

// ── Statements ─────────────────────────────────────────────────────────────

std::string pad_of(int indent) {
    std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
    return pad;
}

std::string translate_if(const IfStmt& if_stmt, GlslCtx& ctx, int indent) {
    const auto pad = pad_of(indent);
    std::ostringstream out;
    out << pad << "if (" << translate_expr(*if_stmt.condition, ctx).text << ") {\n";
    out << translate_stmts(if_stmt.then_body, ctx, indent + 1);
    out << pad << "}\n";
    for (const auto& branch : if_stmt.else_if_branches) {
        out << pad << "else if (" << translate_expr(*branch.condition, ctx).text << ") {\n";
        out << translate_stmts(branch.body, ctx, indent + 1);
        out << pad << "}\n";
    }
    if (!if_stmt.else_body.empty()) {
        out << pad << "else {\n";
        out << translate_stmts(if_stmt.else_body, ctx, indent + 1);
        out << pad << "}\n";
    }
    return out.str();
}

// Semantic analysis guarantees exhaustiveness, so the last arm is a plain `else`.
std::string translate_value_match(const MatchStmt& match_stmt, GlslCtx& ctx, int indent) {
    const auto pad     = pad_of(indent);
    const auto subject = translate_expr(*match_stmt.subject, ctx);
    const auto name    = "match_subject_" + std::to_string(match_stmt.location.line) + "_" +
                      std::to_string(match_stmt.location.column);
    std::ostringstream out;
    out << pad << glsl_type_name(subject) << " " << name << " = " << subject.text << ";\n";
    for (std::size_t index = 0; index < match_stmt.arms.size(); ++index) {
        const auto& arm = match_stmt.arms[index];
        const bool last = index + 1 == match_stmt.arms.size() || arm.is_wildcard();
        out << pad;
        if (!last) {
            out << (index == 0 ? "if ((" : "else if ((") << name << " == " << translate_expr(*arm.pattern, ctx).text
                << ")) ";
        } else if (index > 0) {
            out << "else ";
        }
        out << "{\n" << translate_stmts(arm.body, ctx, indent + 1) << pad << "}\n";
        if (last) {
            break;
        }
    }
    return out.str();
}

// `for i in range(a, b)` or `range(a, b, step)`: the bounds are constants, so the trip count is fixed.
std::string translate_range_loop(const ForeachStmt& loop, GlslCtx& ctx, int indent) {
    const auto* range = std::get_if<CallExpr>(&loop.iterable->expr);
    if (range == nullptr || range->args.size() < 2) {
        throw CodegenError(loop.location, "drawing-rule GLSL codegen: a stage loops only over `range(...)`");
    }
    const auto begin = translate_expr(*range->args[0], ctx).text;
    const auto end   = translate_expr(*range->args[1], ctx).text;
    const auto var   = local_name(loop.var_name);
    std::string header;
    if (range->args.size() == 2) {
        header = "for (int " + var + " = " + begin + "; " + var + " < " + end + "; " + var + " += 1)";
    } else {
        const auto step = translate_expr(*range->args[2], ctx).text;
        header = "for (int " + var + " = " + begin + "; (" + step + " > 0) ? (" + var + " < " + end + ") : ((" +
                 step + " < 0) && (" + var + " > " + end + ")); " + var + " += " + step + ")";
    }
    ctx.locals[loop.var_name] = {.text = var, .type = GlslType::Int};
    const auto pad = pad_of(indent);
    return pad + header + " {\n" + translate_stmts(loop.body, ctx, indent + 1) + pad + "}\n";
}

TranslatedExpr assignment_target(const VarAssign& assign, const GlslCtx& ctx) {
    if (!ctx.alias.empty() && assign.name == ctx.alias && assign.path.size() == 1) {
        return ctx.locals.at(assign.path.front());
    }
    auto target = ctx.locals.at(assign.name);
    for (const auto& segment : assign.path) {
        target = translate_value_member(target, segment, ctx);
    }
    return target;
}

std::string translate_assignment(const VarAssign& assign, GlslCtx& ctx, int indent) {
    if (assign.target != nullptr) {
        throw std::runtime_error("drawing-rule GLSL codegen: trait access in stage handler body");
    }
    const auto value  = translate_expr(*assign.value, ctx);
    const auto target = assignment_target(assign, ctx);
    const auto pad    = pad_of(indent);
    if (const char* helper = int_division_helper(assign.operands, assign.op)) {
        ctx.library->helpers.emplace(helper);
        return pad + target.text + " = " + helper + "(" + target.text + ", " + value.text + ");\n";
    }
    const auto target_type = target.type == GlslType::Int ? GlslType::Int : GlslType::Float;
    return pad + target.text + " " + assign.op + " " + converted(value, target_type) + ";\n";
}

std::string translate_let(const LetStmt& let_stmt, GlslCtx& ctx, int indent) {
    auto value = translate_expr(*let_stmt.value, ctx);
    if (let_stmt.type.has_value() && let_stmt.type->name == "float") {
        value = {.text = converted(value, GlslType::Float), .type = GlslType::Float};
    }
    auto local = value;
    local.text = local_name(let_stmt.name);
    ctx.locals[let_stmt.name] = local;
    return pad_of(indent) + glsl_type_name(value) + " " + local.text + " = " + value.text + ";\n";
}

std::string translate_return(const ReturnStmt& ret, GlslCtx& ctx, int indent) {
    if (!ret.value.has_value()) {
        return pad_of(indent) + "return;\n";
    }
    const auto value = translate_expr(**ret.value, ctx);
    return pad_of(indent) + "return " + converted(value, ctx.result.type) + ";\n";
}

std::string translate_stmts(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx, int indent) {
    std::string out;
    for (const auto& stmt : body) {
        out += std::visit(
            [&](const auto& node) -> std::string {
                using S = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<S, LetStmt>) {
                    return translate_let(node, ctx, indent);
                } else if constexpr (std::is_same_v<S, VarAssign>) {
                    return translate_assignment(node, ctx, indent);
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    return translate_if(node, ctx, indent);
                } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                    return translate_return(node, ctx, indent);
                } else if constexpr (std::is_same_v<S, ExprStmt>) {
                    return pad_of(indent) + translate_expr(*node.expr, ctx).text + ";\n";
                } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                    return translate_range_loop(node, ctx, indent);
                } else if constexpr (std::is_same_v<S, MatchStmt>) {
                    if (node.kind != MatchKind::Value) {
                        throw CodegenError(stmt->location, "drawing-rule GLSL codegen: only a value match runs in a shader");
                    }
                    return translate_value_match(node, ctx, indent);
                } else {
                    throw CodegenError(stmt->location,
                                       "drawing-rule GLSL codegen: unsupported statement in stage handler body");
                }
            },
            stmt->stmt);
    }
    return out;
}

// ── Shader assembly ─────────────────────────────────────────────────────────

void declare_uniforms(std::ostringstream& out, const GlslCtx& ctx) {
    for (const auto& name : ctx.used_uniforms) {
        const auto found = std::ranges::find(*ctx.uniforms, name, &StageUniform::glsl_name);
        out << "uniform " << glsl_type_name(TranslatedExpr{.type = found->type}) << " " << name << ";\n";
    }
}

void seed_builtin_locals(GlslCtx& ctx, bool vertex) {
    const auto& inputs  = vertex ? drawing_vertex_input_fields() : drawing_fragment_input_fields();
    const auto& outputs = vertex ? drawing_vertex_output_fields() : drawing_fragment_output_fields();
    for (const auto* fields : {&inputs, &outputs}) {
        for (const auto& field : *fields) {
            ctx.locals[field.name] = {.text = field.name, .type = type_kind_to_glsl_type(field.type.kind)};
        }
    }
}

// Helpers, then structs, then functions: each only uses what comes before it.
void emit_library(std::ostringstream& out, const ShaderLibrary& library) {
    out << glsl_helper_definitions(library.helpers) << library.structs << library.functions;
}

std::string build_vertex_shader(const EventHandlerNode& handler, bool world, GlslCtx& ctx) {
    seed_builtin_locals(ctx, true);
    // The body is translated first: it discovers the uniforms and functions the header declares.
    const auto body = translate_stmts(handler.body, ctx, 1);
    std::ostringstream out;
    out << "#version 330\n\n";
    out << "in vec3 vertexPosition;\n";
    out << "in vec2 vertexTexCoord;\n\n";
    out << "uniform mat4 mvp;\n";
    declare_uniforms(out, ctx);
    out << "\nout vec2 v_uv;\n";
    out << "out vec4 v_tint;\n\n";
    emit_library(out, *ctx.library);
    out << "void main()\n{\n";
    // corner and uv come in as vertex attributes: rlgl batches draws, so gl_VertexID is not 0..5 per quad.
    out << "    vec2 corner = vertexPosition.xy;\n";
    out << "    vec2 uv = vertexTexCoord;\n";
    out << "    int vertex_index = gl_VertexID;\n";
    out << "    vec2 world_position = vec2(0.0, 0.0);\n";
    out << "    vec2 screen_position = vec2(0.0, 0.0);\n";
    out << "    vec2 uv_out = uv;\n";
    out << "    vec4 tint_out = vec4(1.0, 1.0, 1.0, 1.0);\n";
    out << body;
    out << "    v_uv = uv_out;\n";
    out << "    v_tint = tint_out;\n";
    out << "    gl_Position = mvp * vec4(" << (world ? "world_position" : "screen_position") << ", 0.0, 1.0);\n";
    out << "}\n";
    return out.str();
}

std::string build_fragment_shader(const EventHandlerNode& handler, GlslCtx& ctx) {
    seed_builtin_locals(ctx, false);
    const auto body = translate_stmts(handler.body, ctx, 1);
    std::ostringstream out;
    out << "#version 330\n\n";
    out << "in vec2 v_uv;\n";
    out << "in vec4 v_tint;\n\n";
    declare_uniforms(out, ctx);
    out << "\nout vec4 finalColor;\n\n";
    emit_library(out, *ctx.library);
    out << "void main()\n{\n";
    out << "    vec2 uv = v_uv;\n";
    out << "    vec4 tint = v_tint;\n";
    out << "    vec2 frag_coord = gl_FragCoord.xy;\n";
    out << "    vec4 frag_color = vec4(0.0, 0.0, 0.0, 0.0);\n";
    out << "    vec4 light = vec4(0.0, 0.0, 0.0, 0.0);\n";
    out << body;
    // Premultiplied for the one blend state every drawing rule shares; an alpha above 1 must not scale color.
    out << "    float cover_alpha = clamp(frag_color.a, 0.0, 1.0);\n";
    out << "    float light_alpha = clamp(light.a, 0.0, 1.0);\n";
    out << "    finalColor = vec4(frag_color.rgb * cover_alpha + light.rgb * light_alpha, cover_alpha);\n";
    out << "}\n";
    return out.str();
}

// ── Draw function ───────────────────────────────────────────────────────────

std::unordered_map<std::string, FilterTrait> row_traits(const RuleNode& rule, const DecoratedProgram& program) {
    std::unordered_map<std::string, FilterTrait> traits;
    for (const auto& entry : rule.filter.entries) {
        if (!entry.resolved_trait_id.has_value()) {
            continue;
        }
        traits[entry.alias.value_or(entry.resolved_trait_id->local_name)] = FilterTrait{
            .symbol          = *entry.resolved_trait_id,
            .component_local = component_local(
                EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.qualified_name, program))};
    }
    return traits;
}

// The C++ value a uniform takes: an enum is uploaded as its index.
std::string uniform_value(const StageUniform& uniform, const std::string& lowered) {
    return uniform.source == TypeKind::Enum ? "static_cast<int>(" + lowered + ")" : lowered;
}

std::string set_uniform(const StageUniform& uniform, const std::string& lowered, const char* indent) {
    if (uniform.type == GlslType::Texture) {
        return std::string(indent) + "cactus::runtime::entt_backend::bind_drawing_rule_texture(texture_" +
               uniform.glsl_name + ", " + lowered + ");\n";
    }
    return std::string(indent) + "cactus::runtime::entt_backend::set_drawing_rule_uniform(*shader, loc_" +
           uniform.glsl_name + ", " + uniform_value(uniform, lowered) + ");\n";
}

bool is_window_space_renderer(const SymbolId& rule) {
    return rule == make_symbol_id(SymbolKind::Rule, "std.ui", "RenderUi") ||
           rule == make_symbol_id(SymbolKind::Rule, "std.render.text", "ScreenLabelRender");
}

}  // namespace

std::string drawing_rule_callee(const DecoratedProgram& program, const SymbolId& rule) {
    return system_function_name(program.module_name, rule.local_name, "draw").ref();
}

bool draws_after_window_space(const DecoratedProgram& program, const SymbolId& rule) {
    bool seen = false;
    for (const auto& identity : program.execution_graph.stable_topological_order) {
        if (identity.rule == rule && identity.trigger == drawing_rule_trigger()) {
            return seen;
        }
        seen = seen || (identity.trigger == drawing_rule_trigger() && is_window_space_renderer(identity.rule));
    }
    return false;
}

std::string emit_drawing_rule_function(const RuleNode& rule, const DecoratedProgram& program) {
    const auto* vertex   = find_stage_handler(rule, true);
    const auto* fragment = find_stage_handler(rule, false);
    if (vertex == nullptr || fragment == nullptr || !vertex->alias.has_value() || !fragment->alias.has_value() ||
        program.ast == nullptr) {
        throw CodegenError(rule.location, "drawing-rule GLSL codegen: rule '" + rule.name + "' lacks a stage handler");
    }
    std::vector<StageUniform> uniforms;
    const auto filter_traits = row_traits(rule, program);
    const auto stage_ctx = [&](const EventHandlerNode& handler, ShaderLibrary& library) {
        return GlslCtx{.program       = &program,
                       .library       = &library,
                       .alias         = *handler.alias,
                       .filter_traits = filter_traits,
                       .uniforms      = &uniforms};
    };
    ShaderLibrary vertex_library;
    auto vertex_ctx                 = stage_ctx(*vertex, vertex_library);
    const bool world                = assigns_stage_output(vertex->body, *vertex->alias, "world_position");
    const std::string vertex_glsl   = build_vertex_shader(*vertex, world, vertex_ctx);
    ShaderLibrary fragment_library;
    auto fragment_ctx               = stage_ctx(*fragment, fragment_library);
    const std::string fragment_glsl = build_fragment_shader(*fragment, fragment_ctx);

    std::ostringstream out;
    out << "void " << system_function_name(program.module_name, rule.name, "draw").name
        << "(entt::registry& registry) {\n";
    out << "    static const char* kVertexShader = R\"GLSL(\n" << vertex_glsl << ")GLSL\";\n";
    out << "    static const char* kFragmentShader = R\"GLSL(\n" << fragment_glsl << ")GLSL\";\n";
    out << "    static cactus::runtime::entt_backend::DrawingRuleShaderState shader_state;\n";
    out << "    Shader* shader = cactus::runtime::entt_backend::ensure_drawing_rule_shader(\n";
    out << "        shader_state, kVertexShader, kFragmentShader);\n";
    out << "    if (shader == nullptr) {\n";
    out << "        return;\n";
    out << "    }\n";
    // The shader loads once, so its uniform locations and texture units never change.
    int texture_slot = 0;
    for (const auto& uniform : uniforms) {
        if (uniform.type == GlslType::Texture) {
            out << "    static const int unit_" << uniform.glsl_name
                << " = cactus::runtime::entt_backend::drawing_rule_texture_unit(*shader, \"" << uniform.glsl_name
                << "\", " << texture_slot++ << ");\n";
            out << "    cactus::runtime::entt_backend::DrawingRuleTexture texture_" << uniform.glsl_name
                << "{.unit = unit_" << uniform.glsl_name << "};\n";
            continue;
        }
        out << "    static const int loc_" << uniform.glsl_name
            << " = cactus::runtime::entt_backend::drawing_rule_uniform_location(*shader, \"" << uniform.glsl_name
            << "\");\n";
    }
    out << EnttSystemEmitter::emit_draw_prelude(rule, program);
    out << "    cactus::runtime::entt_backend::begin_drawing_rule(*shader, " << (world ? "true" : "false") << ");\n";
    for (const auto& uniform : uniforms) {
        if (!uniform.per_row) {
            out << set_uniform(uniform, EnttSystemEmitter::emit_world_value(*uniform.read, program), "    ");
        }
    }
    out << EnttSystemEmitter::emit_domain_rows(
        rule, program, "        cactus::runtime::entt_backend::count_drawing_row();\n", [&](const auto& row_value) {
            std::string body;
            for (const auto& uniform : uniforms) {
                if (uniform.per_row) {
                    body += set_uniform(
                        uniform, uniform.read == nullptr ? uniform.component_path : row_value(*uniform.read), "        ");
                }
            }
            return body + "        cactus::runtime::entt_backend::draw_drawing_rule_quad();\n";
        });
    out << "    cactus::runtime::entt_backend::end_drawing_rule();\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
