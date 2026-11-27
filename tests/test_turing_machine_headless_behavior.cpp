#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <utility>

namespace {
using namespace cactus_gen::main;
constexpr int kBlank = 2;
constexpr int kHalt = -1;
constexpr int kRowControls = 15;
namespace panel = cactus_gen::panel;
namespace backend = cactus::runtime::entt_backend;

std::string glyph(int symbol) {
    return symbol == kBlank ? "_" : symbol == 0 ? "0" : "1";
}

std::string state_name(int state) {
    return state == kHalt ? "Halt" : "q" + std::to_string(state);
}

bool same_color(const CactusColor& a, const CactusColor& b) {
    return cactus_raylib_fake::colors_equal(to_raylib_color(a), to_raylib_color(b));
}

std::string expected_label(panel::RowAction action, const Entry* entry) {
    if (entry == nullptr)
        return action == panel::RowAction::Toggle ? "+ Add" : "-";
    switch (action) {
    case panel::RowAction::Toggle:
        return "Remove";
    case panel::RowAction::Write:
        return glyph(entry->write_symbol);
    case panel::RowAction::Move:
        return entry->move < 0 ? "Left" : "Right";
    case panel::RowAction::Previous:
        return "-";
    case panel::RowAction::Next:
        return state_name(entry->dest) + "   +";
    }
    return "-";
}

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
    void press(float x, float y) {
        cactus_raylib_fake::set_mouse_position({x, y});
        cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
        frame();
        cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);
    }
    void release() {
        cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
        frame();
        cactus_raylib_fake::set_mouse_button_released_this_frame(0, false);
    }
    void click(float x, float y) {
        press(x, y);
        release();
    }
    Entry* find_entry(int source, int symbol, entt::entity* id = nullptr) {
        for (auto [entity, entry] : world.view<Entry>().each()) {
            if (entry.source != source || entry.read_symbol != symbol)
                continue;
            if (id != nullptr)
                *id = entity;
            return &entry;
        }
        return nullptr;
    }
    void check_keys() {
        std::map<std::pair<int, int>, int> keys;
        for (auto [id, entry] : world.view<Entry>().each())
            CHECK(++keys[{entry.source, entry.read_symbol}] == 1);
    }
    // Every row button shows the current table.
    void check_editor() {
        check_keys();
        const auto& shows = backend::relation_store<Shows>(world);
        const auto& m = machine();
        int controls = 0;
        for (auto [id, control, button] : world.view<panel::RowControl, cactus_gen::stdlib::ui::Button>().each()) {
            ++controls;
            const int symbol = world.get<panel::ProgramRow>(control.row).symbol;
            entt::entity shown = entt::null;
            const Entry* entry = find_entry(m.page, symbol, &shown);
            INFO("page " << m.page << ", symbol " << symbol << ", action " << static_cast<int>(control.action));
            CHECK(button.label == expected_label(control.action, entry));
            CHECK(shows.has_from(control.row) == (entry != nullptr));
            if (entry != nullptr)
                CHECK(shows.contains(control.row, shown));
            const bool selected = entry != nullptr && m.state_id == m.page && m.read_symbol == symbol;
            CHECK(same_color(button.normal_color, selected ? panel::ACTIVE_COLOR : panel::IDLE_COLOR));
        }
        CHECK(controls == kRowControls);
    }
    // Keys hold after every frame; rows show the change on the next frame.
    void checked_click(float x, float y) {
        press(x, y);
        check_editor();
        release();
        check_keys();
        frame();
        check_editor();
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

TEST_CASE("Rows follow adds, removals, pages and presets frame by frame", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.frame();
    sim.check_editor();
    sim.checked_click(700, 78);  // New program.
    REQUIRE(sim.world.view<Entry>().empty());
    sim.checked_click(170, 556);  // Add q0 / blank.
    sim.checked_click(170, 556);  // Remove it.
    CHECK(sim.world.view<Entry>().empty());
    sim.checked_click(170, 556);  // Add it again.
    CHECK(sim.world.view<Entry>().size() == 1);
    sim.checked_click(275, 556);  // Write zero.
    sim.checked_click(375, 378);  // State page 1.
    sim.checked_click(170, 556);  // Add q1 / blank.
    sim.checked_click(380, 556);  // Move left on q1 only.
    CHECK(sim.find_entry(1, kBlank)->move == -1);
    CHECK(sim.find_entry(0, kBlank)->move == 1);
    sim.checked_click(235, 378);  // Back to page 0, same read symbol.
    sim.checked_click(375, 378);
    for (const float x : {103.F, 261.F, 419.F, 577.F}) {
        sim.checked_click(x, 78);
        sim.checked_click(375, 378);  // Page 1.
        sim.checked_click(235, 378);  // Page 0.
    }
    sim.step();
    sim.frame();
    sim.check_editor();
}

TEST_CASE("Loading a preset highlights the next instruction", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.checked_click(103, 78);  // Binary +1 reads 1 under the head in q0.
    int active = 0;
    for (auto [id, control, button] : sim.world.view<panel::RowControl, cactus_gen::stdlib::ui::Button>().each()) {
        if (!same_color(button.normal_color, panel::ACTIVE_COLOR))
            continue;
        ++active;
        CHECK(sim.world.get<panel::ProgramRow>(control.row).symbol == 1);
    }
    CHECK(active == 5);
}

TEST_CASE("An edit of a destroyed instruction does nothing", "[example-behavior][turing-machine]") {
    Simulator sim;
    entt::entity id = entt::null;
    REQUIRE(sim.find_entry(0, 1, &id) != nullptr);
    sim.world.destroy(id);
    const auto before = sim.world.view<Entry>().size();
    cactus_headless_test::dispatch_boundary_event(sim.world, EditEvent{.entry = id, .action = panel::RowAction::Write});
    sim.frame();
    CHECK(sim.world.view<Entry>().size() == before);
    CHECK(sim.find_entry(0, 1) == nullptr);
    sim.check_editor();
}

TEST_CASE("A Write click on a row whose instruction was destroyed changes nothing", "[example-behavior][turing-machine]") {
    Simulator sim;
    entt::entity id = entt::null;
    REQUIRE(sim.find_entry(0, kBlank, &id) != nullptr);
    sim.press(275, 556);
    sim.world.destroy(id);
    const auto before = sim.world.view<Entry>().size();
    sim.release();
    CHECK(sim.world.view<Entry>().size() == before);
    CHECK(sim.find_entry(0, kBlank) == nullptr);
    sim.check_editor();
}

TEST_CASE("Add on an empty row creates one instruction shown next frame", "[example-behavior][turing-machine]") {
    Simulator sim;
    sim.checked_click(700, 78);
    sim.checked_click(170, 512);  // Add q0 / one.
    REQUIRE(sim.world.view<Entry>().size() == 1);
    const Entry* entry = sim.find_entry(0, 1);
    REQUIRE(entry != nullptr);
    CHECK(entry->write_symbol == 1);
    CHECK(entry->dest == kHalt);
    sim.frame();
    sim.check_editor();
}
