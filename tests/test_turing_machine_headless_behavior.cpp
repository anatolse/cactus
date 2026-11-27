#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {
using namespace cactus_gen::main;
constexpr int kBlank = 2;
constexpr int kHalt = -1;

struct Simulator {
    entt::registry world;
    Simulator() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        cactus_raylib_fake::set_screen_size(980, 600);
        cactus::runtime::entt_backend::reset_pointer_router_state();
        cactus_headless_test::init_and_load(world);
        frame();
    }
    void frame(float dt = 1.F / 60.F) {
        cactus_headless_test::drive_frame(world, dt);
    }
    Machine& machine() {
        return world.get<Machine>(cactus_gen::detail::generated_named_slots().main.Console);
    }
    void step() {
        cactus_raylib_fake::set_key_pressed_this_frame(KEY_SPACE, true);
        frame();
        cactus_raylib_fake::set_key_pressed_this_frame(KEY_SPACE, false);
    }
    void click(float x, float y) {
        cactus_raylib_fake::set_mouse_position({x, y});
        cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
        frame();
        cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);
        cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
        frame();
        cactus_raylib_fake::set_mouse_button_released_this_frame(0, false);
    }
    std::string tape(int begin, int end) {
        std::string result(end - begin, '_');
        for (auto [id, cell] : world.view<Cell>().each()) {
            if (cell.address >= begin && cell.address < end && cell.symbol != kBlank)
                result.at(cell.address - begin) = cell.symbol == 1 ? '1' : '0';
        }
        return result;
    }
};
}  // namespace

TEST_CASE("Binary carry reads and writes under the head", "[example-behavior][turing-machine]") {
    Simulator sim;
    REQUIRE(sim.tape(0, 3) == "011");
    sim.step();
    CHECK(sim.tape(0, 3) == "010");
    CHECK(sim.machine().head == 1);
    sim.step();
    CHECK(sim.tape(0, 3) == "000");
    sim.step();
    CHECK(sim.tape(0, 3) == "100");
    CHECK(sim.machine().head == -1);
    sim.step();
    CHECK(sim.machine().steps == 3);
}

TEST_CASE("All presets halt with their expected tape", "[example-behavior][turing-machine]") {
    Simulator sim;
    const std::string expected[] = {"100", "1111", "01001", "10111"};
    for (int preset = 0; preset < 4; ++preset) {
        sim.machine().load_preset = preset;
        sim.frame();
        for (int i = 0; i < 8; ++i)
            sim.step();
        REQUIRE(sim.machine().state_id == kHalt);
        CHECK(sim.tape(0, static_cast<int>(expected[preset].size())) == expected[preset]);
    }
}

TEST_CASE("Create and repair a program in the visible editor", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.click(700, 78);
    REQUIRE(sim.machine().preset == 4);
    sim.click(75, 226);
    CHECK(sim.machine().missing);
    CHECK(sim.machine().steps == 0);
    sim.click(170, 556);  // Add q0 / blank.
    sim.click(275, 556);  // Write zero.
    sim.click(380, 556);  // Move left.
    sim.click(550, 556);  // Halt to q0.
    sim.click(75, 226);
    CHECK(sim.tape(-1, 1) == "_0");
    CHECK(sim.machine().head == -1);
    sim.click(210, 225);
    REQUIRE(sim.machine().running);
    for (int i = 0; i < 8; ++i)
        sim.frame(0.125F);
    CHECK(sim.machine().steps == 3);
    sim.click(485, 225);
    for (int i = 0; i < 8; ++i)
        sim.frame(0.125F);
    CHECK(sim.machine().steps == 7);
    CHECK(sim.machine().head == -7);
    CHECK(sim.tape(-7, 1) == "_0000000");
    sim.click(210, 225);
    CHECK_FALSE(sim.machine().running);
    auto paused = sim.machine().steps;
    sim.frame(0.125F);
    CHECK(sim.machine().steps == paused);
    sim.click(360, 225);
    CHECK(sim.machine().head == 0);
    CHECK(sim.tape(-8, 1) == "_________");
    sim.click(170, 556);  // Remove the row.
    sim.step();
    CHECK(sim.machine().missing);
    sim.click(375, 378);  // State page 1.
    sim.click(170, 466);
    CHECK(sim.world.view<Entry>().size() == 1);
    sim.click(235, 378);
    CHECK(sim.machine().page == 0);
}

TEST_CASE("Edit the seed, restore it and run parity on even input", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.click(565, 78);
    sim.click(100, 277);
    sim.click(760, 168);  // Address 3: one -> blank -> zero.
    sim.click(760, 168);
    sim.click(100, 277);
    REQUIRE(sim.tape(0, 4) == "1010");
    for (int i = 0; i < 5; ++i)
        sim.step();
    CHECK(sim.tape(0, 5) == "10100");
    sim.click(100, 277);
    sim.click(240, 277);
    sim.click(100, 277);
    CHECK(sim.machine().head == -1);
    CHECK(sim.tape(0, 5) == "1010_");
}

TEST_CASE("The tape view shows addresses and the read head", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.step();
    using namespace cactus_raylib_fake;
    const auto& log = call_log();
    CHECK(find_call<RecordedDrawTextEx>(log, [](const auto& draw) { return draw.text == "^ 1 | 1"; }));
    CHECK(find_call<RecordedDrawRectangleRec>(log, [](const auto& rect) {
        return rect.rec.y == 494 && rect.rec.x == 118 && colors_equal(rect.color, Color{0x08, 0x74, 0x69, 0xFF});
    }));
}

TEST_CASE("Binary carry creates a leading digit beyond the seed", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.click(100, 277);
    sim.click(255, 168);  // Address zero in the seed window centred on address two.
    sim.click(100, 277);
    REQUIRE(sim.tape(0, 3) == "111");
    for (int step = 0; step < 4; ++step)
        sim.step();
    CHECK(sim.tape(-1, 3) == "1000");
    CHECK(sim.machine().head == -2);
}

TEST_CASE("A written cell survives a trip outside the viewport", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.click(700, 78);
    sim.click(170, 556);
    sim.click(275, 556);
    sim.click(550, 556);
    for (int step = 0; step < 7; ++step)
        sim.step();
    REQUIRE(sim.machine().head == 7);
    sim.click(380, 556);
    sim.click(170, 466);
    sim.click(380, 466);
    sim.click(550, 466);
    for (int step = 0; step < 7; ++step)
        sim.step();
    CHECK(sim.machine().head == 0);
    CHECK(sim.machine().read_symbol == 0);
    CHECK(sim.tape(0, 8) == "00000000");
}

TEST_CASE("Choosing a preset during playback starts it paused", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.click(220, 225);
    REQUIRE(sim.machine().running);
    sim.click(410, 78);
    CHECK_FALSE(sim.machine().running);
    CHECK(sim.machine().steps == 0);
    CHECK(sim.tape(0, 5) == "10110");
}
