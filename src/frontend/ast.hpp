#pragma once

#include "common/source_location.hpp"
#include "common/types.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cactus {

// Forward declarations
struct ExprNode;
struct StmtNode;
struct ArchetypeTraitEntry;

// ── Type Reference (unresolved, from parser) ────────────────────────────────

struct TypeRef {
    std::string name;                               // e.g. "int", "vec3", "list", "Item"
    std::optional<std::unique_ptr<TypeRef>> param;  // for list[T]
    SourceLocation location;
};

// ── Field Modifiers ─────────────────────────────────────────────────────────

struct FieldModifiers {
    bool is_let     = false;
    bool is_var     = false;
    bool is_persist = false;
    bool is_sync    = false;
    bool is_pub     = false;
};

// ── Field Declaration ───────────────────────────────────────────────────────

struct FieldNode {
    FieldModifiers modifiers;
    std::string name;
    TypeRef type;
    std::optional<std::unique_ptr<ExprNode>> default_value;
    SourceLocation location;
};

// ── Function Parameter ──────────────────────────────────────────────────────

struct FuncParam {
    std::string name;
    TypeRef type;
    SourceLocation location;
};

struct FieldAssignment {
    std::string name;
    std::unique_ptr<ExprNode> value;
    SourceLocation location;
};

struct ArchetypeTraitEntry {
    std::string trait_name;
    std::vector<FieldAssignment> assignments;
    SourceLocation location;
};

struct ArchetypeTemplateUseEntry {
    std::string template_name;
    SourceLocation location;
};

struct ArchetypeBodyEntry {
    enum class Kind { Trait, TemplateUse };

    Kind kind         = Kind::Trait;
    std::size_t index = 0;  // Index into EntityNode/TemplateNode traits or template_uses.
    SourceLocation location;
};

struct SpawnExpr {
    std::string template_name;
    std::vector<ArchetypeTraitEntry> overrides;
    SourceLocation location;
};

// ── Expression Nodes ────────────────────────────────────────────────────────

struct LiteralExpr {
    enum class Kind { Int, Float, String, HexColor, Bool };
    Kind kind;
    std::string value;
    SourceLocation location;
};

struct IdentExpr {
    std::string name;
    SourceLocation location;
};

struct SelfExpr {
    SourceLocation location;
};

struct BinaryExpr {
    std::string op;  // "+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "and", "or"
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;
    SourceLocation location;
};

struct UnaryExpr {
    std::string op;  // "not", "-"
    std::unique_ptr<ExprNode> operand;
    SourceLocation location;
};

struct CallExpr {
    std::unique_ptr<ExprNode> callee;
    std::vector<std::unique_ptr<ExprNode>> args;
    SourceLocation location;
};

struct MemberExpr {
    std::unique_ptr<ExprNode> object;
    std::string member;
    SourceLocation location;
};

struct LambdaExpr {
    std::vector<std::string> params;  // single or multiple param names
    std::unique_ptr<ExprNode> body;
    SourceLocation location;
};

struct PipelineExpr {
    std::unique_ptr<ExprNode> source;
    struct PipelineOp {
        std::string method;  // "map", "filter", "reduce"
        std::vector<std::unique_ptr<ExprNode>> args;
    };
    std::vector<PipelineOp> operations;
    SourceLocation location;
};

struct MatchArm {
    std::unique_ptr<ExprNode> pattern;
    std::unique_ptr<ExprNode> body;
    SourceLocation location;
};

struct MatchExpr {
    std::unique_ptr<ExprNode> subject;
    std::vector<MatchArm> arms;
    SourceLocation location;
};

struct IfExpr {
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<ExprNode> then_expr;
    std::unique_ptr<ExprNode> else_expr;
    SourceLocation location;
};

struct ListExpr {
    std::vector<std::unique_ptr<ExprNode>> elements;
    SourceLocation location;
};

struct ExprNode {
    using Variant = std::variant<LiteralExpr,
                                 IdentExpr,
                                 SelfExpr,
                                 BinaryExpr,
                                 UnaryExpr,
                                 CallExpr,
                                 MemberExpr,
                                 LambdaExpr,
                                 PipelineExpr,
                                 MatchExpr,
                                 IfExpr,
                                 ListExpr,
                                 SpawnExpr>;
    Variant expr;
    SourceLocation location;

    ExprNode() = default;
    explicit ExprNode(Variant v, SourceLocation loc)
        : expr(std::move(v))
        , location(std::move(loc)) {}
};

// ── Statement Nodes ─────────────────────────────────────────────────────────

struct VarAssign {
    std::string name;
    std::string op;  // "=", "+=", "-="
    std::unique_ptr<ExprNode> value;
    SourceLocation location;
};

struct LetStmt {
    std::string name;
    std::unique_ptr<ExprNode> value;
    SourceLocation location;
};

struct EmitStmt {
    std::string event_name;
    std::optional<std::unique_ptr<ExprNode>> target;
    std::vector<FieldAssignment> payload;
    SourceLocation location;
};

struct ReturnStmt {
    std::optional<std::unique_ptr<ExprNode>> value;
    SourceLocation location;
};

struct ExprStmt {
    std::unique_ptr<ExprNode> expr;
    SourceLocation location;
};

struct IfStmt {
    std::unique_ptr<ExprNode> condition;
    std::vector<std::unique_ptr<StmtNode>> then_body;
    std::vector<std::unique_ptr<StmtNode>> else_body;
    SourceLocation location;
};

struct ForeachStmt {
    std::string var_name;
    std::unique_ptr<ExprNode> iterable;
    std::vector<std::unique_ptr<StmtNode>> body;
    SourceLocation location;
};

struct TraitMatchArm {
    std::string trait_name;
    std::optional<std::string> alias;
    std::vector<std::unique_ptr<StmtNode>> body;
    SourceLocation location;
};

struct WildcardMatchArm {
    std::vector<std::unique_ptr<StmtNode>> body;
    SourceLocation location;
};

struct TraitMatchStmt {
    std::unique_ptr<ExprNode> subject;
    std::vector<TraitMatchArm> arms;
    std::optional<WildcardMatchArm> wildcard;
    SourceLocation location;
};

// spawn TemplateName: Trait: field = expr
struct SpawnStmt {
    std::string template_name;
    std::vector<ArchetypeTraitEntry> overrides;
    SourceLocation location;
};

// destroy — removes current entity
struct DestroyStmt {
    std::optional<std::unique_ptr<ExprNode>> target_expr;
    SourceLocation location;
};

// load module.name — deferred scene transition
struct LoadStmt {
    std::string module_name;
    SourceLocation location;
};

// add TraitName[: ...] [to expr] — attach or patch trait on entity
struct AddTraitStmt {
    std::string trait_name;
    std::vector<FieldAssignment> args;
    std::optional<std::unique_ptr<ExprNode>> target_expr;
    SourceLocation location;
};

// remove TraitName [from expr] — detach trait from entity
struct RemoveTraitStmt {
    std::string trait_name;
    std::optional<std::unique_ptr<ExprNode>> target_expr;
    SourceLocation location;
};

// project TraitName[: ...] [to expr] — frame-local projected trait overlay
struct ProjectTraitStmt {
    std::string trait_name;
    std::vector<FieldAssignment> args;
    std::optional<std::unique_ptr<ExprNode>> target_expr;
    SourceLocation location;
};

struct StmtNode {
    using Variant = std::variant<LetStmt,
                                 VarAssign,
                                 EmitStmt,
                                 SpawnStmt,
                                 DestroyStmt,
                                 LoadStmt,
                                 AddTraitStmt,
                                 RemoveTraitStmt,
                                 ProjectTraitStmt,
                                 ReturnStmt,
                                 ExprStmt,
                                 IfStmt,
                                 ForeachStmt,
                                 TraitMatchStmt>;
    Variant stmt;
    SourceLocation location;

    StmtNode() = default;
    explicit StmtNode(Variant v, SourceLocation loc)
        : stmt(std::move(v))
        , location(std::move(loc)) {}
};

// ── Event Handler ───────────────────────────────────────────────────────────

struct EventHandlerNode {
    std::string event_name;
    std::optional<std::string> alias;  // optional 'as alias' clause
    std::vector<std::unique_ptr<StmtNode>> body;
    SourceLocation location;
};

// ── Top-Level Declaration Nodes ─────────────────────────────────────────────

struct ModuleNode {
    std::string name;
    SourceLocation location;
};

struct UseNode {
    std::string module_name;
    std::optional<std::string> alias;
    SourceLocation location;
};

struct ConstAssignment {
    std::string name;
    std::unique_ptr<ExprNode> value;
    SourceLocation location;
};

struct ConstBlockNode {
    std::vector<ConstAssignment> assignments;
    SourceLocation location;
};

struct StructNode {
    std::string name;
    std::vector<FieldNode> fields;
    SourceLocation location;
};

struct EnumVariant {
    std::string name;
    std::optional<int> value;
    SourceLocation location;
};

struct EnumNode {
    std::string name;
    std::vector<EnumVariant> variants;
    SourceLocation location;
};

struct TraitNode {
    std::string name;
    bool is_pub    = false;
    bool is_stdlib = false;
    std::vector<FieldNode> fields;
    SourceLocation location;
};

struct EntityNode {
    std::string name;
    bool is_pub = false;
    std::optional<std::string> template_ref;  // set when "entity Name from TemplateName:"
    std::vector<ArchetypeBodyEntry> body_entries;
    std::vector<ArchetypeTemplateUseEntry> template_uses;
    std::vector<ArchetypeTraitEntry> traits;
    SourceLocation location;
};

// Template declaration — multi-instance blueprint, not auto-instantiated.
// Similar to EntityNode; instantiated at runtime via `spawn`.
struct TemplateNode {
    std::string name;
    bool is_pub = false;
    std::vector<ArchetypeBodyEntry> body_entries;
    std::vector<ArchetypeTemplateUseEntry> template_uses;
    std::vector<ArchetypeTraitEntry> traits;
    SourceLocation location;
};

struct FilterEntry {
    std::string qualified_name;        // "phys.Body" or "Body" — full dotted path as parsed
    std::optional<std::string> alias;  // "b" from "as b"
    SourceLocation location;
};

struct FilterClause {
    std::vector<FilterEntry> entries;      // full parsed info (qualified names + aliases)
    std::vector<std::string> trait_names;  // simple trait names (last component) — backward compat
    SourceLocation location;
};

struct SortKey {
    std::string alias;
    std::string field;
    bool descending = false;
    SourceLocation location;
};

struct SystemNode {
    std::string name;
    bool is_stdlib = false;
    FilterClause filter;   // empty entries = no filter (match all)
    FilterClause exclude;  // empty entries = no exclude
    std::vector<SortKey> order_by;
    std::vector<std::string> after_systems;  // explicit ordering: this system runs after these
    std::optional<std::string> target;       // "cpu" or "gpu"
    std::vector<EventHandlerNode> handlers;
    SourceLocation location;
};

struct ExternSystemNode {
    std::string name;
    bool is_stdlib = false;
    FilterClause filter;
    FilterClause exclude;
    std::vector<SortKey> order_by;
    std::vector<std::string> after_systems;
    std::optional<std::string> target;
    SourceLocation location;
};

struct ViewElement {
    std::string tag_name;
    std::vector<FieldAssignment> props;
    std::vector<ViewElement> children;
    SourceLocation location;
};

struct ViewNode {
    std::string name;
    std::vector<FuncParam> params;
    std::vector<ViewElement> elements;
    SourceLocation location;
};

struct EventNode {
    std::string name;
    bool is_pub = false;
    std::vector<FieldNode> fields;
    SourceLocation location;
};

struct FuncNode {
    std::string name;
    bool is_pub    = false;
    bool is_extern = false;
    bool is_stdlib = false;
    std::vector<FuncParam> params;
    std::optional<TypeRef> return_type;
    std::vector<std::unique_ptr<StmtNode>> body;
    SourceLocation location;
};

struct FuncSignature {
    std::string name;
    std::vector<FuncParam> params;
    std::optional<TypeRef> return_type;
    SourceLocation location;
};

struct InterfaceNode {
    std::string name;
    std::vector<FuncSignature> methods;
    SourceLocation location;
};

// ── Asset Declaration ────────────────────────────────────────────────────────

/// Asset type kinds for asset_decl
enum class AssetKind { Mesh, Texture, Sound, Music, Font, Material };

/// asset [pub] Name: asset_type = "path"
struct AssetDeclNode {
    std::string name;
    bool is_pub          = false;
    AssetKind asset_kind = AssetKind::Mesh;
    std::string path;  // resource path string literal
    SourceLocation location;
};

// ── Input Declaration ────────────────────────────────────────────────────────

/// Input action kind for input_decl
enum class InputKind { Button, Axis };

/// A single property in an input declaration: key = expression
struct InputPropNode {
    std::string key;
    std::unique_ptr<ExprNode> value;
    SourceLocation location;
};

/// input [pub] Name: button|axis  INDENT { key = expr } DEDENT
struct InputDeclNode {
    std::string name;
    bool is_pub          = false;
    InputKind input_kind = InputKind::Button;
    std::vector<InputPropNode> props;
    SourceLocation location;
};

// ── Program (AST Root) ─────────────────────────────────────────────────────

using Declaration = std::variant<ModuleNode,
                                 UseNode,
                                 ConstBlockNode,
                                 StructNode,
                                 EnumNode,
                                 TraitNode,
                                 EntityNode,
                                 TemplateNode,
                                 SystemNode,
                                 ExternSystemNode,
                                 ViewNode,
                                 EventNode,
                                 FuncNode,
                                 InterfaceNode,
                                 AssetDeclNode,
                                 InputDeclNode>;

struct ProgramNode {
    std::vector<Declaration> declarations;
    SourceLocation location;
};

}  // namespace cactus
