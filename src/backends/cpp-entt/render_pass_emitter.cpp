#include "backends/cpp-entt/render_pass_emitter.hpp"

#include "backends/cpp-entt/type_utils.hpp"
#include "common/render_pass_builtin_fields.hpp"
#include "common/render_pass_intrinsics.hpp"
#include "common/vector_binary_ops.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

namespace {

// ── GLSL value shapes the restricted stage-handler subset actually needs ───

enum class GlslType : std::uint8_t { Float, Bool, Vec2, Color };

const char* glsl_type_name(GlslType type) {
    switch (type) {
        case GlslType::Float:
            return "float";
        case GlslType::Bool:
            return "bool";
        case GlslType::Vec2:
            return "vec2";
        case GlslType::Color:
            return "vec4";
    }
    std::unreachable();
}

GlslType glsl_type_from(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Float:
            return GlslType::Float;
        case TypeKind::Vec2:
            return GlslType::Vec2;
        case TypeKind::Color:
            return GlslType::Color;
        default:
            throw std::runtime_error("render-pass GLSL codegen: unsupported field type '" + type.name + "'");
    }
}

struct TranslatedExpr {
    std::string text;
    GlslType type;
};

// A per-entity value the vertex shader needs that isn't a built-in input —
// read from the vertex handler's own filtered trait, uploaded as a uniform
// once per matching entity.
struct NeededUniform {
    std::string glsl_name;  // "u_xf_position"
    GlslType type;
    SymbolId trait;
    std::string field_name;
};

struct GlslCtx {
    const DecoratedProgram* program = nullptr;
    const ProgramNode* ast          = nullptr;
    std::string alias;                          // the stage handler's own alias ("v" / "f")
    bool is_vertex = false;
    // vertex only: filter alias -> resolved trait (exactly the traits the
    // vertex handler's `filter:` bound; dsl-render-passes 2.4 guarantees a
    // vertex handler has a non-empty filter and a fragment handler has none).
    std::unordered_map<std::string, SymbolId> filter_traits;
    std::unordered_map<std::string, GlslType> locals;
    // Names of `let`-bound locals specifically (a subset of `locals`, which
    // also holds the stage's built-in fields, pre-seeded under their own
    // fixed, known-safe names) — these came from the author and are emitted
    // through local_reference()'s "let_" mangling so an ordinary Cactus name
    // can never collide with a GLSL reserved word (e.g. `half`, reserved in
    // GLSL though an entirely ordinary identifier in Cactus).
    std::unordered_set<std::string> let_names;
    // Active inlined-func parameter bindings, name -> the caller-scope value
    // already translated at the call site (design.md Decision 3 allows
    // calling any pure `func`; the backend inlines its body rather than
    // emitting a real GLSL function). Scoped to the current inline_func_call
    // via save/restore, so nested/sibling inline calls don't leak into each
    // other or shadow the outer stage-handler body incorrectly.
    std::unordered_map<std::string, TranslatedExpr> param_substitutions;
    std::vector<NeededUniform> uniforms;
};

// A `let`-bound local is emitted under a mangled name so an author's
// ordinary Cactus identifier can never collide with a GLSL reserved word
// (`half`, `fixed`, `long`, and others GLSL reserves whether or not the
// generated shader actually uses the corresponding feature) — built-in
// stage fields (`corner`, `screen_position`, etc.) are pre-chosen to be
// GLSL-safe and use their own name unchanged.
std::string local_reference(const std::string& name, const GlslCtx& ctx) {
    return ctx.let_names.contains(name) ? "let_" + name : name;
}

TranslatedExpr promote_to(TranslatedExpr value, GlslType target) {
    if (value.type == target) {
        return value;
    }
    if (target == GlslType::Float && value.type != GlslType::Float) {
        throw std::runtime_error("render-pass GLSL codegen: cannot use a non-numeric value where a float is required");
    }
    return value;
}

// Total, lossless round-trip for exactly the four GlslType values this
// domain admits (glsl_type_from already narrows every field/expression type
// down to Float/Bool/Vec2/Color before any expression translation happens —
// a Vec3 can never reach here), so translate_binary can consult the shared
// dsl-vector-expressions operand-type table (lookup_vector_binary_op_result)
// instead of its own narrower same-type-or-float-promotion rule.
TypeKind glsl_type_to_type_kind(GlslType type) {
    switch (type) {
        case GlslType::Float:
            return TypeKind::Float;
        case GlslType::Bool:
            return TypeKind::Bool;
        case GlslType::Vec2:
            return TypeKind::Vec2;
        case GlslType::Color:
            return TypeKind::Color;
    }
    std::unreachable();
}

GlslType type_kind_to_glsl_type(TypeKind kind) {
    switch (kind) {
        case TypeKind::Float:
            return GlslType::Float;
        case TypeKind::Bool:
            return GlslType::Bool;
        case TypeKind::Vec2:
            return GlslType::Vec2;
        case TypeKind::Color:
            return GlslType::Color;
        default:
            throw std::runtime_error("render-pass GLSL codegen: vector-operator result type has no GLSL representation");
    }
}

std::string hex_color_literal_to_glsl(const std::string& raw_hex) {
    std::string hex = raw_hex;
    if (hex.size() == 6) {
        hex += "FF";
    }
    if (hex.size() != 8) {
        throw std::runtime_error("render-pass GLSL codegen: malformed hex color literal '#" + raw_hex + "'");
    }
    const auto channel = [&](std::size_t offset) {
        const int byte_value = std::stoi(hex.substr(offset, 2), nullptr, 16);
        std::ostringstream out;
        out << (static_cast<double>(byte_value) / 255.0);
        return out.str();
    };
    return "vec4(" + channel(0) + ", " + channel(2) + ", " + channel(4) + ", " + channel(6) + ")";
}

const ConstBlockNode* find_const_block_with(const ProgramNode& ast, const std::string& name) {
    for (const auto& decl : ast.declarations) {
        const auto* block = std::get_if<ConstBlockNode>(&decl);
        if (block == nullptr) {
            continue;
        }
        for (const auto& assignment : block->assignments) {
            if (assignment.name == name) {
                return block;
            }
        }
    }
    return nullptr;
}

const ExprNode* find_const_initializer(const ProgramNode& ast, const std::string& name) {
    const auto* block = find_const_block_with(ast, name);
    if (block == nullptr) {
        return nullptr;
    }
    for (const auto& assignment : block->assignments) {
        if (assignment.name == name) {
            return assignment.value.get();
        }
    }
    return nullptr;
}

const FuncNode* find_func_decl(const ProgramNode& ast, const SymbolId& symbol) {
    for (const auto& decl : ast.declarations) {
        const auto* func = std::get_if<FuncNode>(&decl);
        if (func != nullptr && func->resolved_func_id.has_value() && *func->resolved_func_id == symbol) {
            return func;
        }
    }
    return nullptr;
}

TranslatedExpr translate_expr(const ExprNode& expr, GlslCtx& ctx);

std::string translate_call_args(const std::vector<std::unique_ptr<ExprNode>>& args, GlslCtx& ctx) {
    std::string joined;
    for (const auto& arg : args) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += translate_expr(*arg, ctx).text;
    }
    return joined;
}

// Inlines a plain (non-extern) func call: supports exactly the shape the
// language's small pure helpers actually use — a single terminal
// `return expr` (design.md Decision 3 allows calling any pure `func` from a
// stage handler; the backend is responsible for translating its body, not
// just its call site). Each argument is translated once, in the *caller's*
// scope, then substituted for the parameter name while translating the
// callee's body — this is inlining a pure expression, not compiling a real
// GLSL function, so re-evaluating a simple argument expression at each of its
// uses inside the body is an acceptable, deliberate simplification.
TranslatedExpr inline_func_call(const FuncNode& func, const std::vector<std::unique_ptr<ExprNode>>& args, GlslCtx& ctx) {
    if (func.params.size() != args.size()) {
        throw std::runtime_error("render-pass GLSL codegen: '" + func.name + "' called with the wrong argument count");
    }
    if (func.body.size() != 1) {
        throw std::runtime_error("render-pass GLSL codegen: '" + func.name +
                                 "' body is not a single terminal return — unsupported by inline stage-handler "
                                 "translation");
    }
    const auto* ret = std::get_if<ReturnStmt>(&func.body.front()->stmt);
    if (ret == nullptr || !ret->value.has_value()) {
        throw std::runtime_error("render-pass GLSL codegen: '" + func.name +
                                 "' body is not a single terminal return — unsupported by inline stage-handler "
                                 "translation");
    }

    auto saved_substitutions = ctx.param_substitutions;
    for (std::size_t i = 0; i < func.params.size(); ++i) {
        ctx.param_substitutions[func.params[i].name] = translate_expr(*args[i], ctx);
    }
    auto result             = translate_expr(**ret->value, ctx);
    ctx.param_substitutions = std::move(saved_substitutions);
    return result;
}

// vec2 -> .x/.y, color(vec4) -> .r/.g/.b/.a; anything else has no component access.
bool is_component_access(GlslType object_type, const std::string& member) {
    if (object_type == GlslType::Vec2) {
        return member == "x" || member == "y";
    }
    if (object_type == GlslType::Color) {
        return member == "r" || member == "g" || member == "b" || member == "a";
    }
    return false;
}

const ResolvedField* find_trait_field(const ResolvedTrait* trait, const std::string& name) {
    if (trait == nullptr) {
        return nullptr;
    }
    for (const auto& candidate : trait->fields) {
        if (candidate.name == name) {
            return &candidate;
        }
    }
    return nullptr;
}

// `owner.field` where `owner` is the stage handler's own alias: a built-in
// input field (Decision 2's fixed field table for the current stage).
TranslatedExpr translate_builtin_field_access(const std::string& field_name, const GlslCtx& ctx) {
    const auto& fields = ctx.is_vertex ? quads_vertex_input_fields() : quads_fragment_input_fields();
    for (const auto& field : fields) {
        if (field.name == field_name) {
            return {.text = field_name, .type = glsl_type_from(field.type)};
        }
    }
    throw std::runtime_error("render-pass GLSL codegen: '" + ctx.alias + "." + field_name +
                             "' is not a readable built-in field here");
}

// `owner.field` where `owner` is a vertex-handler filter alias: reads a
// per-instance trait field, registering (and deduplicating) the uniform that
// carries it into the shader.
TranslatedExpr translate_filter_trait_access(const SymbolId& trait_symbol,
                                             const std::string& owner_name,
                                             const std::string& field_name,
                                             GlslCtx& ctx) {
    const auto* trait = EnttCodegenUtils::find_trait(*ctx.program, make_canonical_id(trait_symbol));
    const auto* field  = find_trait_field(trait, field_name);
    if (field == nullptr) {
        throw std::runtime_error("render-pass GLSL codegen: unknown trait field '" + owner_name + "." + field_name +
                                 "'");
    }
    const auto glsl_type    = glsl_type_from(field->type);
    const auto uniform_name = "u_" + owner_name + "_" + field_name;
    if (std::ranges::none_of(
            ctx.uniforms, [&](const NeededUniform& existing) { return existing.glsl_name == uniform_name; })) {
        ctx.uniforms.push_back(NeededUniform{
            .glsl_name = uniform_name, .type = glsl_type, .trait = trait_symbol, .field_name = field->name});
    }
    return {.text = uniform_name, .type = glsl_type};
}

TranslatedExpr translate_member(const MemberExpr& member, GlslCtx& ctx) {
    const auto* owner = std::get_if<IdentExpr>(&member.object->expr);
    if (owner == nullptr) {
        // Component access on a nested expression (e.g. a color channel of a
        // computed value): translate the object, then append the accessor.
        const auto object = translate_expr(*member.object, ctx);
        if (is_component_access(object.type, member.member)) {
            return {.text = object.text + "." + member.member, .type = GlslType::Float};
        }
        throw std::runtime_error("render-pass GLSL codegen: unsupported member access '." + member.member + "'");
    }

    // An inlined func's parameter: `owner` is the argument's already-
    // translated value (from the caller's scope), not a name resolvable
    // here — component access on it goes through the same path as any other
    // computed sub-expression.
    if (const auto param_it = ctx.param_substitutions.find(owner->name); param_it != ctx.param_substitutions.end()) {
        if (is_component_access(param_it->second.type, member.member)) {
            return {.text = param_it->second.text + "." + member.member, .type = GlslType::Float};
        }
        throw std::runtime_error("render-pass GLSL codegen: unsupported member access '" + owner->name + "." +
                                 member.member + "'");
    }
    if (owner->name == ctx.alias) {
        return translate_builtin_field_access(member.member, ctx);
    }
    if (const auto trait_it = ctx.filter_traits.find(owner->name); trait_it != ctx.filter_traits.end()) {
        return translate_filter_trait_access(trait_it->second, owner->name, member.member, ctx);
    }
    if (const auto local_it = ctx.locals.find(owner->name);
        local_it != ctx.locals.end() && is_component_access(local_it->second, member.member)) {
        return {.text = local_reference(owner->name, ctx) + "." + member.member, .type = GlslType::Float};
    }

    throw std::runtime_error("render-pass GLSL codegen: unresolved member access '" + owner->name + "." +
                             member.member + "'");
}

TranslatedExpr translate_call(const CallExpr& call, GlslCtx& ctx) {
    if (const auto* callee_ident = std::get_if<IdentExpr>(&call.callee->expr)) {
        if (callee_ident->name == "vec2") {
            return {.text = "vec2(" + translate_call_args(call.args, ctx) + ")", .type = GlslType::Vec2};
        }
        if (callee_ident->name == "vec3") {
            throw std::runtime_error("render-pass GLSL codegen: vec3 is not used by any Quads built-in field");
        }
        if (callee_ident->name == "color") {
            return {.text = "vec4(" + translate_call_args(call.args, ctx) + ")", .type = GlslType::Color};
        }
    }

    if (!call.resolved_callee_id.has_value()) {
        throw std::runtime_error("render-pass GLSL codegen: unresolved call in stage handler body");
    }
    const auto& callee = *call.resolved_callee_id;
    if (is_render_pass_portable_glsl_intrinsic(callee)) {
        // std.math.sqrt/clamp share both name and signature with GLSL's own
        // built-ins — the registration is "portable to GLSL," not "renamed."
        return {.text = callee.local_name + "(" + translate_call_args(call.args, ctx) + ")", .type = GlslType::Float};
    }
    const auto* func = find_func_decl(*ctx.ast, callee);
    if (func == nullptr) {
        throw std::runtime_error("render-pass GLSL codegen: '" + make_canonical_id(callee) +
                                 "' has no registered portable GLSL translation");
    }
    return inline_func_call(*func, call.args, ctx);
}

TranslatedExpr translate_literal(const LiteralExpr& literal) {
    switch (literal.kind) {
        case LiteralExpr::Kind::Int:
            return {.text = literal.value + ".0", .type = GlslType::Float};
        case LiteralExpr::Kind::Float:
            return {.text = literal.value, .type = GlslType::Float};
        case LiteralExpr::Kind::HexColor:
            return {.text = hex_color_literal_to_glsl(literal.value), .type = GlslType::Color};
        case LiteralExpr::Kind::Bool:
            return {.text = literal.value, .type = GlslType::Bool};
        case LiteralExpr::Kind::String:
            throw std::runtime_error("render-pass GLSL codegen: unsupported literal in stage handler body");
    }
    std::unreachable();
}

TranslatedExpr translate_ident(const IdentExpr& ident, GlslCtx& ctx) {
    if (const auto param_it = ctx.param_substitutions.find(ident.name); param_it != ctx.param_substitutions.end()) {
        return param_it->second;
    }
    if (const auto local_it = ctx.locals.find(ident.name); local_it != ctx.locals.end()) {
        return {.text = local_reference(ident.name, ctx), .type = local_it->second};
    }
    if (const auto* initializer = find_const_initializer(*ctx.ast, ident.name)) {
        return translate_expr(*initializer, ctx);
    }
    throw std::runtime_error("render-pass GLSL codegen: unresolved identifier '" + ident.name + "'");
}

TranslatedExpr translate_binary(const BinaryExpr& binary, GlslCtx& ctx) {
    auto left  = translate_expr(*binary.left, ctx);
    auto right = translate_expr(*binary.right, ctx);
    const bool is_comparison = binary.op == "<" || binary.op == ">" || binary.op == "<=" || binary.op == ">=" ||
                               binary.op == "==" || binary.op == "!=";
    if (left.type != right.type) {
        if (is_comparison) {
            left  = promote_to(std::move(left), GlslType::Float);
            right = promote_to(std::move(right), GlslType::Float);
        } else if (auto result_kind = lookup_vector_binary_op_result(
                       glsl_type_to_type_kind(left.type), binary.op, glsl_type_to_type_kind(right.type))) {
            return {.text = "(" + left.text + " " + binary.op + " " + right.text + ")",
                   .type = type_kind_to_glsl_type(*result_kind)};
        } else {
            throw std::runtime_error("render-pass GLSL codegen: no operator '" + binary.op + "' for operand types '" +
                                     std::string(glsl_type_name(left.type)) + "' and '" +
                                     std::string(glsl_type_name(right.type)) + "'");
        }
    }
    return {.text = "(" + left.text + " " + binary.op + " " + right.text + ")",
           .type = is_comparison ? GlslType::Bool : left.type};
}

TranslatedExpr translate_expr(const ExprNode& expr, GlslCtx& ctx) {
    return std::visit(
        [&](const auto& node) -> TranslatedExpr {
            using E = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                return translate_literal(node);
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return translate_ident(node, ctx);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                const auto operand = translate_expr(*node.operand, ctx);
                return {.text = "(" + node.op + operand.text + ")", .type = operand.type};
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return translate_binary(node, ctx);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                return translate_member(node, ctx);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                return translate_call(node, ctx);
            } else {
                throw std::runtime_error("render-pass GLSL codegen: unsupported expression form in stage handler body");
            }
        },
        expr.expr);
}

std::string translate_stmts(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx, int indent);

std::string translate_if(const IfStmt& if_stmt, GlslCtx& ctx, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
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

std::string translate_stmts(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
    std::ostringstream out;
    for (const auto& stmt : body) {
        if (const auto* let_stmt = std::get_if<LetStmt>(&stmt->stmt)) {
            const auto value             = translate_expr(*let_stmt->value, ctx);
            ctx.locals[let_stmt->name] = value.type;
            ctx.let_names.insert(let_stmt->name);
            out << pad << glsl_type_name(value.type) << " " << local_reference(let_stmt->name, ctx) << " = "
                << value.text << ";\n";
        } else if (const auto* assign = std::get_if<VarAssign>(&stmt->stmt)) {
            const auto value = translate_expr(*assign->value, ctx);
            const std::string target = assign->name == ctx.alias && assign->path.size() == 1
                                          ? assign->path.front()
                                          : local_reference(assign->name, ctx);
            out << pad << target << " " << assign->op << " " << value.text << ";\n";
        } else if (const auto* if_stmt = std::get_if<IfStmt>(&stmt->stmt)) {
            out << translate_if(*if_stmt, ctx, indent);
        } else {
            throw std::runtime_error("render-pass GLSL codegen: unsupported statement in stage handler body");
        }
    }
    return out.str();
}

// ── Shader assembly ─────────────────────────────────────────────────────────

std::string build_vertex_shader(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx) {
    ctx.locals["corner"]          = GlslType::Vec2;
    ctx.locals["uv"]              = GlslType::Vec2;
    ctx.locals["screen_position"] = GlslType::Vec2;
    ctx.locals["uv_out"]          = GlslType::Vec2;
    ctx.locals["tint_out"]        = GlslType::Color;
    // Translate the body first — it's what discovers and populates
    // ctx.uniforms (via translate_member's filter-trait-access path), and
    // those uniform declarations must appear in the header, above main(),
    // which is only assembled afterward.
    std::ostringstream body_out;
    // corner/uv ride in on raylib's own vertexPosition/vertexTexCoord
    // attributes (draw_render_pass_quad_instance feeds them via
    // rlVertex2f/rlTexCoord2f) rather than a gl_VertexID-indexed const array:
    // rlgl batches draws, so gl_VertexID is offset by however many vertices
    // already sit in the shared batch buffer, not 0..5 relative to this
    // draw's own 6 vertices — real per-vertex attributes sidestep that
    // entirely and match how every other custom shader in this backend
    // (see kLightingVertexShader) already receives per-vertex data.
    body_out << "    vec2 corner = vertexPosition.xy;\n";
    body_out << "    vec2 uv = vertexTexCoord;\n";
    body_out << "    int vertex_index = gl_VertexID;\n";
    body_out << "    vec2 screen_position = vec2(0.0, 0.0);\n";
    body_out << "    vec2 uv_out = vec2(0.0, 0.0);\n";
    body_out << "    vec4 tint_out = vec4(0.0, 0.0, 0.0, 0.0);\n";
    body_out << translate_stmts(body, ctx, 1);
    body_out << "    v_uv = uv_out;\n";
    body_out << "    v_tint = tint_out;\n";
    body_out << "    gl_Position = mvp * vec4(screen_position, 0.0, 1.0);\n";

    std::ostringstream out;
    out << "#version 330\n\n";
    out << "in vec3 vertexPosition;\n";
    out << "in vec2 vertexTexCoord;\n\n";
    out << "uniform mat4 mvp;\n";
    for (const auto& uniform : ctx.uniforms) {
        out << "uniform " << glsl_type_name(uniform.type) << " " << uniform.glsl_name << ";\n";
    }
    out << "\nout vec2 v_uv;\n";
    out << "out vec4 v_tint;\n\n";
    out << "void main()\n{\n";
    out << body_out.str();
    out << "}\n";
    return out.str();
}

std::string build_fragment_shader(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx) {
    std::ostringstream out;
    out << "#version 330\n\n";
    out << "in vec2 v_uv;\n";
    out << "in vec4 v_tint;\n\n";
    out << "out vec4 finalColor;\n\n";
    out << "void main()\n{\n";
    out << "    vec2 uv = v_uv;\n";
    out << "    vec4 tint = v_tint;\n";
    out << "    vec2 frag_coord = gl_FragCoord.xy;\n";
    out << "    vec4 frag_color = vec4(0.0, 0.0, 0.0, 0.0);\n";
    ctx.locals["uv"]         = GlslType::Vec2;
    ctx.locals["tint"]       = GlslType::Color;
    ctx.locals["frag_coord"] = GlslType::Vec2;
    ctx.locals["frag_color"] = GlslType::Color;
    out << translate_stmts(body, ctx, 1);
    out << "    finalColor = frag_color;\n";
    out << "}\n";
    return out.str();
}

// ── AST lookup ───────────────────────────────────────────────────────────────

struct StageHandlerRef {
    const RuleNode* rule            = nullptr;
    const EventHandlerNode* handler = nullptr;
};

std::optional<StageHandlerRef> find_stage_handler(const ProgramNode& ast, const ResolvedHandlerTrigger& trigger) {
    for (const auto& decl : ast.declarations) {
        const auto* rule = std::get_if<RuleNode>(&decl);
        if (rule == nullptr) {
            continue;
        }
        for (const auto& handler : rule->handlers) {
            if (handler.resolved_trigger.has_value() && *handler.resolved_trigger == trigger) {
                return StageHandlerRef{.rule = rule, .handler = &handler};
            }
        }
    }
    return std::nullopt;
}

std::unordered_map<std::string, SymbolId> vertex_filter_traits(const RuleNode& rule) {
    std::unordered_map<std::string, SymbolId> traits;
    for (const auto& entry : rule.filter.entries) {
        if (!entry.resolved_trait_id.has_value()) {
            continue;
        }
        const auto& name    = entry.alias.value_or(entry.resolved_trait_id->local_name);
        traits[name] = *entry.resolved_trait_id;
    }
    return traits;
}

}  // namespace

std::optional<std::string> emit_render_pass_dispatch_body(const DecoratedProgram& program, const ResolvedPhase& phase) {
    if (!phase.render_pass.has_value() || program.ast == nullptr) {
        return std::nullopt;
    }
    const auto& info = *phase.render_pass;

    const auto vertex_ref   = find_stage_handler(*program.ast, info.vertex_trigger);
    const auto fragment_ref = find_stage_handler(*program.ast, info.fragment_trigger);
    if (!vertex_ref.has_value() || !fragment_ref.has_value()) {
        // Cardinality was already validated during semantic analysis; a
        // missing handler here means the program has errors and codegen
        // should not have been reached at all.
        throw std::runtime_error("render-pass GLSL codegen: phase '" + phase.name +
                                 "' is missing a stage handler despite passing validation");
    }
    if (!vertex_ref->handler->alias.has_value() || !fragment_ref->handler->alias.has_value()) {
        throw std::runtime_error("render-pass GLSL codegen: phase '" + phase.name + "' stage handler has no alias");
    }

    const auto filter_traits = vertex_filter_traits(*vertex_ref->rule);
    if (filter_traits.empty()) {
        throw std::runtime_error("render-pass GLSL codegen: phase '" + phase.name +
                                 "' vertex handler has no resolved filter trait");
    }

    GlslCtx vertex_ctx{.program       = &program,
                      .ast           = program.ast,
                      .alias         = *vertex_ref->handler->alias,
                      .is_vertex     = true,
                      .filter_traits = filter_traits};
    const std::string vertex_glsl = build_vertex_shader(vertex_ref->handler->body, vertex_ctx);

    GlslCtx fragment_ctx{
        .program = &program, .ast = program.ast, .alias = *fragment_ref->handler->alias, .is_vertex = false};
    const std::string fragment_glsl = build_fragment_shader(fragment_ref->handler->body, fragment_ctx);

    // A render-pass phase is only ever recognized (recognize_render_pass_phases)
    // after collect_types has assigned it a canonical symbol_id, so this is
    // always present for a well-formed program — checked explicitly rather
    // than via optional::value_or, whose fallback argument would otherwise be
    // eagerly constructed (and, with an empty module name, throw) even when
    // unused.
    if (!phase.symbol_id.has_value()) {
        throw std::runtime_error("render-pass GLSL codegen: phase '" + phase.name + "' has no resolved symbol");
    }
    const std::string phase_cpp = EnttCodegenUtils::symbol_cpp_name(*phase.symbol_id);
    // dsl-render-passes' vertex handler is unary: exactly one filtered trait
    // drives the per-instance view (further filter traits, if any, would need
    // a multi-component view — not needed by either shipped example).
    const auto& primary_alias_and_trait = *filter_traits.begin();
    const auto* filter_trait = EnttCodegenUtils::find_trait(program, make_canonical_id(primary_alias_and_trait.second));
    if (filter_trait == nullptr) {
        throw std::runtime_error("render-pass GLSL codegen: phase '" + phase.name +
                                 "' vertex handler's filter trait could not be resolved");
    }
    const std::string trait_cpp = EnttCodegenUtils::trait_cpp_name(primary_alias_and_trait.second);

    std::ostringstream out;
    out << "    static const char* kVertexShader_" << phase_cpp << " = R\"GLSL(\n" << vertex_glsl << ")GLSL\";\n";
    out << "    static const char* kFragmentShader_" << phase_cpp << " = R\"GLSL(\n" << fragment_glsl << ")GLSL\";\n";
    out << "    static cactus::runtime::entt_backend::RenderPassShaderState shader_state_" << phase_cpp << ";\n";
    out << "    Shader* render_pass_shader = cactus::runtime::entt_backend::ensure_render_pass_shader(\n";
    out << "        shader_state_" << phase_cpp << ", kVertexShader_" << phase_cpp << ", kFragmentShader_" << phase_cpp
        << ");\n";
    out << "    if (render_pass_shader == nullptr) {\n";
    out << "        return;\n";
    out << "    }\n";
    for (const auto& uniform : vertex_ctx.uniforms) {
        out << "    const int loc_" << uniform.glsl_name << " = GetShaderLocation(*render_pass_shader, \""
            << uniform.glsl_name << "\");\n";
    }
    out << "    cactus::runtime::raylib::BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());\n";
    out << "    BeginShaderMode(*render_pass_shader);\n";
    out << "    for (auto entity : registry.view<" << trait_cpp << ">()) {\n";
    out << "        (void)entity;\n";
    out << "        const auto& " << trait_cpp << "_comp = registry.get<" << trait_cpp << ">(entity);\n";
    for (const auto& uniform : vertex_ctx.uniforms) {
        switch (uniform.type) {
            case GlslType::Float:
                out << "        {\n";
                out << "            const float value = " << trait_cpp << "_comp." << uniform.field_name << ";\n";
                out << "            SetShaderValue(*render_pass_shader, loc_" << uniform.glsl_name
                    << ", &value, SHADER_UNIFORM_FLOAT);\n";
                out << "        }\n";
                break;
            case GlslType::Vec2:
                out << "        {\n";
                out << "            const std::array<float, 2> value = {" << trait_cpp << "_comp." << uniform.field_name
                    << ".x, " << trait_cpp << "_comp." << uniform.field_name << ".y};\n";
                out << "            SetShaderValue(*render_pass_shader, loc_" << uniform.glsl_name
                    << ", value.data(), SHADER_UNIFORM_VEC2);\n";
                out << "        }\n";
                break;
            case GlslType::Color:
                out << "        {\n";
                out << "            const std::array<float, 4> value = {\n";
                out << "                static_cast<float>(" << trait_cpp << "_comp." << uniform.field_name
                    << ".r) / 255.0F,\n";
                out << "                static_cast<float>(" << trait_cpp << "_comp." << uniform.field_name
                    << ".g) / 255.0F,\n";
                out << "                static_cast<float>(" << trait_cpp << "_comp." << uniform.field_name
                    << ".b) / 255.0F,\n";
                out << "                static_cast<float>(" << trait_cpp << "_comp." << uniform.field_name
                    << ".a) / 255.0F,\n";
                out << "            };\n";
                out << "            SetShaderValue(*render_pass_shader, loc_" << uniform.glsl_name
                    << ", value.data(), SHADER_UNIFORM_VEC4);\n";
                out << "        }\n";
                break;
            case GlslType::Bool:
                // glsl_type_from() never maps a trait field to Bool — no
                // ResolvedField's TypeInfo::kind produces it — so a uniform
                // can't reach this case for a well-formed program.
                throw std::runtime_error("render-pass GLSL codegen: a bool-typed uniform is not supported");
        }
    }
    out << "        cactus::runtime::entt_backend::draw_render_pass_quad_instance();\n";
    out << "    }\n";
    out << "    EndShaderMode();\n";
    out << "    cactus::runtime::raylib::EndMode2D();\n";
    return out.str();
}

}  // namespace cactus
