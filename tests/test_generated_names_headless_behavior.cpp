#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace main_ns   = cactus_gen::main;
namespace shapes_ns = cactus_gen::shapes;
using FixedTick     = cactus_gen::stdlib::core::FixedTickPhase;

void cactus_gen::main::NativeTurn::on(const FixedTick& /*trigger*/,
                                      entt::entity /*entity*/,
                                      const main_ns::Box& box,
                                      main_ns::Heading& heading,
                                      const Capabilities<FixedTick>& /*capabilities*/) {
    heading.angle += 1.0F + static_cast<float>(box.ticks);
}

void cactus_gen::main::NativeTurn::on(const shapes_ns::ResetEvent& trigger,
                                      entt::entity /*entity*/,
                                      main_ns::Heading& heading,
                                      const Capabilities<shapes_ns::ResetEvent>& /*capabilities*/) {
    heading.angle = static_cast<float>(trigger.value);
}

namespace {

std::string generated_text() {
    std::ifstream file(CACTUS_HEADLESS_GENERATED_CPP);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("Generated names contain no module-mangled identifiers", "[codegen][names]") {
    const auto text = generated_text();
    REQUIRE_FALSE(text.empty());
    const std::regex identifier(R"([A-Za-z_][A-Za-z0-9_]*)");
    std::set<std::string> reserved;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), identifier); it != std::sregex_iterator(); ++it) {
        if (it->str().find("__", 2) != std::string::npos) {
            reserved.insert(it->str());
        }
    }
    for (const auto& name : reserved) {
        INFO(name);
        CHECK(false);
    }
    CHECK(text.find("#define Camera") == std::string::npos);
    CHECK(text.find("#undef PI") == std::string::npos);
}

TEST_CASE("Namespaced program runs across modules", "[runtime][names]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto first = cactus_gen::detail::generated_named_slots().main.First;
    cactus_headless_test::drive_frame(registry, 1.0F / 60.0F);

    CHECK(registry.get<main_ns::Box>(first).ticks == 4);
    CHECK(registry.get<main_ns::Box>(first).extent.width == 4.0F);
    CHECK(registry.all_of<main_ns::Mode::Busy>(first));
    CHECK(registry.get<main_ns::Heading>(first).angle >= 1.0F);
    CHECK(registry.get<shapes_ns::Session>(cactus_gen::detail::generated_named_slots().main.Game).tone ==
          shapes_ns::Tone::default_);
    CHECK(registry.get<shapes_ns::projected_Session>(cactus_gen::detail::generated_named_slots().main.Game).default_ ==
          1);
    CHECK(shapes_ns::PI_ == 3.0F);
}
