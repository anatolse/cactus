#include "backends/cpp-entt/index_cell_size.hpp"

#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>

namespace cactus {

namespace {

constexpr const char* kNs = "cactus::runtime::entt_backend::";

// NOLINTNEXTLINE(misc-no-recursion) -- an expression tree
bool constant_expression(const ExprNode& expr) {
    return std::visit(
        [](const auto& node) -> bool {
            using E = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                return node.kind == LiteralExpr::Kind::Int || node.kind == LiteralExpr::Kind::Float;
            } else if constexpr (std::is_same_v<E, IdentExpr> || std::is_same_v<E, MemberExpr>) {
                return node.resolved_const_id.has_value();
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return constant_expression(*node.operand);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return constant_expression(*node.left) && constant_expression(*node.right);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                const auto* callee = std::get_if<IdentExpr>(&node.callee->expr);
                return callee != nullptr && callee->name == "vec3" &&
                       std::ranges::all_of(node.args, [](const auto& arg) { return constant_expression(*arg); });
            } else {
                return false;
            }
        },
        expr.expr);
}

// The trait entries the program declares for an archetype node, after template composition.
const std::vector<ArchetypeTraitEntry>* node_traits(const ProgramNode& ast, const ArchetypeNodeId& node) {
    const std::vector<ArchetypeTraitEntry>* traits   = nullptr;
    const std::vector<ChildArchetypeNode>* children = nullptr;
    for (const auto& decl : ast.declarations) {
        if (const auto* entity = std::get_if<EntityNode>(&decl);
            entity != nullptr && entity->resolved_entity_id == node.archetype) {
            traits   = &entity->traits;
            children = &entity->children;
            break;
        }
        if (const auto* tmpl = std::get_if<TemplateNode>(&decl);
            tmpl != nullptr && tmpl->resolved_template_id == node.archetype) {
            traits   = &tmpl->traits;
            children = &tmpl->children;
            break;
        }
    }
    for (const auto& role : node.role_path) {
        if (children == nullptr) {
            return nullptr;
        }
        const auto child = std::ranges::find(*children, role, &ChildArchetypeNode::role);
        if (child == children->end()) {
            return nullptr;
        }
        traits   = &child->traits;
        children = &child->children;
    }
    return traits;
}

// C++ for one field of a shape: its constant assignment, or the trait's
// default read from the generated component. Nullopt for a runtime value.
std::optional<std::string> shape_field(const ArchetypeTraitEntry& entry,
                                       const SymbolId& shape,
                                       const std::string& field,
                                       const DecoratedProgram& program) {
    const auto assigned = std::ranges::find(entry.assignments, field, &FieldAssignment::name);
    if (assigned == entry.assignments.end()) {
        return EnttCodegenUtils::trait_cpp_name(shape, shape.local_name, program) + "{}." + cpp_escaped(field);
    }
    if (assigned->value == nullptr || !constant_expression(*assigned->value)) {
        return std::nullopt;
    }
    return EnttCodegenUtils::emit_expr(*assigned->value, program);
}

// C++ for the largest extent of one archetype's collider, or nullopt when it is not a constant.
std::optional<std::string> collider_extent(const ArchetypeTraitEntry& entry,
                                           const SymbolId& shape,
                                           const DecoratedProgram& program) {
    const auto field = [&](const std::string& name) { return shape_field(entry, shape, name, program); };
    if (shape.local_name == "BoxCollider") {
        const auto size = field("size");
        return size.has_value() ? std::optional{std::string{kNs} + "box_extent(" + *size + ")"} : std::nullopt;
    }
    const auto radius = field("radius");
    if (!radius.has_value()) {
        return std::nullopt;
    }
    if (shape.local_name == "SphereCollider") {
        return std::string{kNs} + "sphere_extent(" + *radius + ")";
    }
    const auto height = field("height");
    if (!height.has_value()) {
        return std::nullopt;
    }
    return std::string{kNs} + "capsule_extent(" + *radius + ", " + *height + ")";
}

bool adds_any(const DecoratedProgram& program, const std::vector<SymbolId>& traits) {
    return std::ranges::any_of(program.execution_graph.handlers, [&](const auto& handler) {
        return std::ranges::any_of(handler.contract.commands, [&](const InferredHandlerCommand& command) {
            return command.kind == HandlerCommandKind::Add && command.target.has_value() &&
                   std::ranges::contains(traits, *command.target);
        });
    });
}

}  // namespace

std::vector<SymbolId> collider_shape_traits() {
    const ModuleId physics{.name = std::string(kPhysicsVolumeModule)};
    std::vector<SymbolId> shapes;
    shapes.reserve(kColliderShapeTraits.size());
    for (const auto shape : kColliderShapeTraits) {
        shapes.push_back(make_symbol_id(SymbolKind::Trait, physics, std::string(shape)));
    }
    return shapes;
}

std::vector<std::size_t> member_origins(const DecoratedProgram& program, const std::vector<SymbolId>& required) {
    const auto& facts = program.reachability;
    const auto carries = [&](const ReachableTraitSet& set) {
        return std::ranges::all_of(required, [&](const SymbolId& trait) {
            return std::ranges::contains(set, trait) || std::ranges::contains(facts.free_traits, trait);
        });
    };
    std::vector<std::size_t> origins;
    for (std::size_t index = 0; index < facts.origins.size(); ++index) {
        if (std::ranges::any_of(facts.origins[index].trait_sets, carries)) {
            origins.push_back(index);
        }
    }
    return origins;
}

std::optional<std::string> constant_collider_cell_size(const DecoratedProgram& program,
                                                       const std::vector<std::size_t>& origins) {
    const auto shapes = collider_shape_traits();
    if (!program.reachability.derived || program.ast == nullptr || adds_any(program, shapes)) {
        return std::nullopt;
    }
    std::vector<std::string> extents;
    for (const auto index : origins) {
        const auto& node = program.reachability.origins[index].node;
        if (node.archetype.kind != SymbolKind::Entity && node.archetype.kind != SymbolKind::Template) {
            return std::nullopt;
        }
        const auto* traits = node_traits(*program.ast, node);
        if (traits == nullptr) {
            return std::nullopt;
        }
        for (const auto& entry : *traits) {
            const auto shape = entry.resolved_trait_id.has_value()
                                   ? std::ranges::find(shapes, *entry.resolved_trait_id)
                                   : shapes.end();
            if (shape == shapes.end()) {
                continue;
            }
            auto extent = collider_extent(entry, *shape, program);
            if (!extent.has_value()) {
                return std::nullopt;
            }
            extents.push_back(std::move(*extent));
        }
    }
    if (extents.empty()) {
        return std::nullopt;
    }
    return std::string{kNs} + "median_extent(std::array{" + join_strings(extents, ", ") + "})";
}

}  // namespace cactus
