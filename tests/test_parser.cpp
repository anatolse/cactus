#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"

using namespace cactus;

static ProgramNode parse(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    return program;
}


TEST_CASE("Parser: module declaration", "[parser]") {
    auto prog = parse("module player\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ModuleNode>(prog.declarations[0]);
    CHECK(decl.name == "player");
}

TEST_CASE("Parser: use declaration", "[parser]") {
    auto prog = parse("use world\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "world");
    CHECK_FALSE(decl.alias.has_value());
}

TEST_CASE("Parser: use with alias", "[parser]") {
    auto prog = parse("use world as w\n");
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "world");
    REQUIRE(decl.alias.has_value());
    CHECK(*decl.alias == "w");
}

TEST_CASE("Parser: const block", "[parser]") {
    auto prog = parse("const:\n    X = 42\n    Y = 3.14\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    REQUIRE(decl.assignments.size() == 2);
    CHECK(decl.assignments[0].name == "X");
    CHECK(decl.assignments[1].name == "Y");
}

TEST_CASE("Parser: struct declaration", "[parser]") {
    auto prog = parse("struct Item:\n    name: int\n    price: float\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<StructNode>(prog.declarations[0]);
    CHECK(decl.name == "Item");
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].name == "name");
    CHECK(decl.fields[0].type.name == "int");
    CHECK(decl.fields[1].name == "price");
    CHECK(decl.fields[1].type.name == "float");
}

TEST_CASE("Parser: enum declaration", "[parser]") {
    auto prog = parse("enum Color:\n    Red\n    Green\n    Blue\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EnumNode>(prog.declarations[0]);
    CHECK(decl.name == "Color");
    REQUIRE(decl.variants.size() == 3);
    CHECK(decl.variants[0].name == "Red");
    CHECK(decl.variants[1].name == "Green");
    CHECK(decl.variants[2].name == "Blue");
}

TEST_CASE("Parser: enum with explicit values", "[parser]") {
    auto prog = parse("enum Priority:\n    Low = 0\n    High = 10\n");
    auto& decl = std::get<EnumNode>(prog.declarations[0]);
    REQUIRE(decl.variants[0].value.has_value());
    CHECK(*decl.variants[0].value == 0);
    REQUIRE(decl.variants[1].value.has_value());
    CHECK(*decl.variants[1].value == 10);
}

TEST_CASE("Parser: trait with var fields", "[parser]") {
    auto prog = parse("trait Health:\n    var health: int = 100\n    let max_health: int = 100\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<TraitNode>(prog.declarations[0]);
    CHECK(decl.name == "Health");
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].modifiers.is_var);
    CHECK(decl.fields[0].name == "health");
    CHECK(decl.fields[1].modifiers.is_let);
    CHECK(decl.fields[1].name == "max_health");
}

TEST_CASE("Parser: trait with persist sync modifiers", "[parser]") {
    auto prog = parse("trait Position:\n    persist sync var x: float\n    persist sync var y: float\n");
    auto& decl = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].modifiers.is_persist);
    CHECK(decl.fields[0].modifiers.is_sync);
    CHECK(decl.fields[0].modifiers.is_var);
    CHECK(decl.fields[0].name == "x");
}

TEST_CASE("Parser: pub unit with apply and config", "[parser]") {
    auto prog = parse(
        "pub unit Player:\n"
        "    apply:\n"
        "        Health\n"
        "        Position\n"
        "    config:\n"
        "        health = 100\n"
        "        x = 0.0\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<UnitNode>(prog.declarations[0]);
    CHECK(decl.name == "Player");
    CHECK(decl.is_pub);
    REQUIRE(decl.apply.trait_names.size() == 2);
    CHECK(decl.apply.trait_names[0] == "Health");
    CHECK(decl.apply.trait_names[1] == "Position");
    REQUIRE(decl.config.has_value());
    CHECK(decl.config->assignments.size() == 2);
}

TEST_CASE("Parser: system with filter and handler", "[parser]") {
    auto prog = parse(
        "system MoveSystem:\n"
        "    filter: [Position, Velocity]\n"
        "    on tick(dt: float):\n"
        "        x = x + vx * dt\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<SystemNode>(prog.declarations[0]);
    CHECK(decl.name == "MoveSystem");
    REQUIRE(decl.filter.trait_names.size() == 2);
    CHECK(decl.filter.trait_names[0] == "Position");
    CHECK(decl.filter.trait_names[1] == "Velocity");
    REQUIRE(decl.handlers.size() == 1);
    CHECK(decl.handlers[0].event_name == "tick");
}

TEST_CASE("Parser: event declaration", "[parser]") {
    auto prog = parse("event Damage:\n    var amount: int\n    var source: int\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EventNode>(prog.declarations[0]);
    CHECK(decl.name == "Damage");
    REQUIRE(decl.fields.size() == 2);
}

TEST_CASE("Parser: func declaration", "[parser]") {
    auto prog = parse("func add(a: int, b: int) -> int:\n    return a + b\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    CHECK(decl.name == "add");
    REQUIRE(decl.params.size() == 2);
    REQUIRE(decl.return_type.has_value());
    CHECK(decl.return_type->name == "int");
    REQUIRE(decl.body.size() == 1);
}

TEST_CASE("Parser: expression — binary arithmetic", "[parser]") {
    auto prog = parse("const:\n    X = 1 + 2 * 3\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto& expr = decl.assignments[0].value;
    // Should be (1 + (2 * 3)) due to precedence
    auto* bin = std::get_if<BinaryExpr>(&expr->expr);
    REQUIRE(bin != nullptr);
    CHECK(bin->op == "+");
}

TEST_CASE("Parser: expression — function call", "[parser]") {
    auto prog = parse("func test():\n    foo(1, 2)\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* expr_stmt = std::get_if<ExprStmt>(&decl.body[0]->stmt);
    REQUIRE(expr_stmt != nullptr);
    auto* call = std::get_if<CallExpr>(&expr_stmt->expr->expr);
    REQUIRE(call != nullptr);
    CHECK(call->args.size() == 2);
}

TEST_CASE("Parser: expression — member access", "[parser]") {
    auto prog = parse("const:\n    X = a.b\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* mem = std::get_if<MemberExpr>(&decl.assignments[0].value->expr);
    REQUIRE(mem != nullptr);
    CHECK(mem->member == "b");
}

TEST_CASE("Parser: expression — unary not", "[parser]") {
    auto prog = parse("const:\n    X = not true\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* un = std::get_if<UnaryExpr>(&decl.assignments[0].value->expr);
    REQUIRE(un != nullptr);
    CHECK(un->op == "not");
}

TEST_CASE("Parser: expression — list literal", "[parser]") {
    auto prog = parse("const:\n    X = [1, 2, 3]\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* list = std::get_if<ListExpr>(&decl.assignments[0].value->expr);
    REQUIRE(list != nullptr);
    CHECK(list->elements.size() == 3);
}

TEST_CASE("Parser: if statement", "[parser]") {
    auto prog = parse("func test():\n    if x > 0:\n        return x\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* if_stmt = std::get_if<IfStmt>(&decl.body[0]->stmt);
    REQUIRE(if_stmt != nullptr);
    CHECK(if_stmt->then_body.size() == 1);
}

TEST_CASE("Parser: emit statement", "[parser]") {
    auto prog = parse("func test():\n    emit Damage(10, 0)\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    auto* emit = std::get_if<EmitStmt>(&decl.body[0]->stmt);
    REQUIRE(emit != nullptr);
    CHECK(emit->event_name == "Damage");
    CHECK(emit->args.size() == 2);
}

TEST_CASE("Parser: assignment operators", "[parser]") {
    auto prog = parse("func test():\n    x = 1\n    y += 2\n    z -= 3\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 3);
    auto* a1 = std::get_if<VarAssign>(&decl.body[0]->stmt);
    auto* a2 = std::get_if<VarAssign>(&decl.body[1]->stmt);
    auto* a3 = std::get_if<VarAssign>(&decl.body[2]->stmt);
    REQUIRE(a1 != nullptr);
    CHECK(a1->op == "=");
    REQUIRE(a2 != nullptr);
    CHECK(a2->op == "+=");
    REQUIRE(a3 != nullptr);
    CHECK(a3->op == "-=");
}

TEST_CASE("Parser: interface declaration", "[parser]") {
    auto prog = parse("interface Renderable:\n    func draw(x: int, y: int)\n    func update(dt: float)\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<InterfaceNode>(prog.declarations[0]);
    CHECK(decl.name == "Renderable");
    REQUIRE(decl.methods.size() == 2);
    CHECK(decl.methods[0].name == "draw");
    CHECK(decl.methods[1].name == "update");
}

TEST_CASE("Parser: multiple declarations", "[parser]") {
    auto prog = parse(
        "module game\n"
        "use player\n"
        "const:\n"
        "    X = 10\n"
        "struct Item:\n"
        "    price: int\n");
    CHECK(prog.declarations.size() == 4);
}
