// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
//
// Reads the real compiler output for a std.persistence-using fixture as
// plain text, rather than compiling or running it: generated main() opens a
// real window, which headless behavioral tests never do (see
// CACTUS_GENERATED_NO_MAIN in every other headless fixture). This is the
// only place that exercises the *default* entry point's own body, since
// every other persistence test builds with CACTUS_GENERATED_NO_MAIN and
// supplies its own driver instead.
#ifndef CACTUS_GENERATED_CPP_PATH
#error "CACTUS_GENERATED_CPP_PATH must name the generated translation unit"
#endif

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read_generated_source() {
    std::ifstream in(CACTUS_GENERATED_CPP_PATH);
    REQUIRE(in.is_open());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

TEST_CASE("generated main() registers the example file adapter by default for a program using std.persistence",
          "[codegen][persistence][main]") {
    const auto source = read_generated_source();

    CHECK(source.find("#include \"backends/cpp-entt/persistence_file_adapter.hpp\"") != std::string::npos);

    const auto main_start = source.find("int main() try {");
    REQUIRE(main_start != std::string::npos);
    const auto main_end = source.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_end != std::string::npos);
    const auto main_fn = source.substr(main_start, main_end - main_start);

    const auto setup_dispatcher_pos = main_fn.find("generated_setup_dispatcher(dispatcher);");
    const auto register_pos         = main_fn.find("register_persistence_adapter(");
    const auto make_adapter_pos     = main_fn.find("make_example_file_adapter(\"saves\")");
    const auto init_project_pos     = main_fn.find("generated_init_project(registry);");
    REQUIRE(setup_dispatcher_pos != std::string::npos);
    REQUIRE(register_pos != std::string::npos);
    REQUIRE(make_adapter_pos != std::string::npos);
    REQUIRE(init_project_pos != std::string::npos);

    // Registered before init/load so a SaveRequested emitted from `on load:`
    // (dsl-stdlib-persistence) has a working adapter, not adapter_unavailable.
    CHECK(setup_dispatcher_pos < register_pos);
    CHECK(register_pos < make_adapter_pos);
    CHECK(make_adapter_pos < init_project_pos);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
