#include "fake_raylib/fake_raylib.hpp"

#include <unordered_map>

namespace cactus_raylib_fake {

namespace {

struct FakeState {
    std::unordered_map<int, bool> key_down;
    std::unordered_map<int, bool> key_pressed;
    std::unordered_map<int, bool> key_released;
    std::unordered_map<int, bool> mouse_button_down;
    std::unordered_map<int, bool> mouse_button_pressed;
    std::unordered_map<int, bool> mouse_button_released;
    Vector2 mouse_position{.x = 0.0F, .y = 0.0F};
    Vector2 mouse_delta{.x = 0.0F, .y = 0.0F};
    float mouse_wheel_move{0.0F};

    bool window_ready{false};
    int screen_width{800};
    int screen_height{600};

    std::vector<RecordedCall> log;
};

FakeState& state() noexcept {
    static FakeState instance;
    return instance;
}

template <typename Map>
bool lookup(const Map& map, int key) noexcept {
    const auto it = map.find(key);
    return it != map.end() && it->second;
}

template <typename Entry>
void record(Entry entry) {
    state().log.emplace_back(std::move(entry));
}

}  // namespace

void reset() noexcept {
    state() = FakeState{};
}

void set_key_down(int key, bool is_down) noexcept {
    state().key_down[key] = is_down;
}

void set_key_pressed_this_frame(int key, bool is_pressed) noexcept {
    state().key_pressed[key] = is_pressed;
}

void set_key_released_this_frame(int key, bool is_released) noexcept {
    state().key_released[key] = is_released;
}

void set_mouse_button_down(int button, bool is_down) noexcept {
    state().mouse_button_down[button] = is_down;
}

void set_mouse_button_pressed_this_frame(int button, bool is_pressed) noexcept {
    state().mouse_button_pressed[button] = is_pressed;
}

void set_mouse_button_released_this_frame(int button, bool is_released) noexcept {
    state().mouse_button_released[button] = is_released;
}

void set_mouse_position(Vector2 position) noexcept {
    state().mouse_position = position;
}

void set_mouse_delta(Vector2 delta) noexcept {
    state().mouse_delta = delta;
}

void set_mouse_wheel_move(float delta) noexcept {
    state().mouse_wheel_move = delta;
}

void set_window_ready(bool ready) noexcept {
    state().window_ready = ready;
}

void set_screen_size(int width, int height) noexcept {
    state().screen_width  = width;
    state().screen_height = height;
}

const std::vector<RecordedCall>& call_log() noexcept {
    return state().log;
}

bool IsKeyDown(int key) noexcept {
    return lookup(state().key_down, key);
}

bool IsKeyPressed(int key) noexcept {
    return lookup(state().key_pressed, key);
}

bool IsKeyReleased(int key) noexcept {
    return lookup(state().key_released, key);
}

bool IsMouseButtonDown(int button) noexcept {
    return lookup(state().mouse_button_down, button);
}

bool IsMouseButtonPressed(int button) noexcept {
    return lookup(state().mouse_button_pressed, button);
}

bool IsMouseButtonReleased(int button) noexcept {
    return lookup(state().mouse_button_released, button);
}

Vector2 GetMousePosition() noexcept {
    return state().mouse_position;
}

Vector2 GetMouseDelta() noexcept {
    return state().mouse_delta;
}

float GetMouseWheelMove() noexcept {
    return state().mouse_wheel_move;
}

bool IsWindowReady() noexcept {
    return state().window_ready;
}

int GetScreenWidth() noexcept {
    return state().screen_width;
}

int GetScreenHeight() noexcept {
    return state().screen_height;
}

Vector2 MeasureTextEx(const Font /*font*/, const char* text, const float fontSize, const float /*spacing*/) noexcept {
    // Deterministic stand-in for real glyph-metric measurement (see
    // raylib_io.hpp): half a font-size unit per character, one font-size unit
    // tall. Monotonic in both text length and font size, which is all layout
    // regression tests need to observe — not a claim of pixel accuracy.
    const std::size_t length = text != nullptr ? std::char_traits<char>::length(text) : 0;
    return Vector2{.x = static_cast<float>(length) * fontSize * 0.5F, .y = fontSize};
}

void ClearBackground(Color color) noexcept {
    record(RecordedClearBackground{.color = color});
}

void BeginMode2D(Camera2D camera) noexcept {
    record(RecordedBeginMode2D{.camera = camera});
}

void EndMode2D() noexcept {
    record(RecordedEndMode2D{});
}

void BeginMode3D(Camera3D camera) noexcept {
    record(RecordedBeginMode3D{.camera = camera});
}

void EndMode3D() noexcept {
    record(RecordedEndMode3D{});
}

void BeginTextureMode(RenderTexture2D target) noexcept {
    record(RecordedBeginTextureMode{.target = target});
}

void EndTextureMode() noexcept {
    record(RecordedEndTextureMode{});
}

void BeginScissorMode(int x, int y, int width, int height) noexcept {
    record(RecordedBeginScissorMode{.x = x, .y = y, .width = width, .height = height});
}

void EndScissorMode() noexcept {
    record(RecordedEndScissorMode{});
}

void DrawMesh(Mesh mesh, Material material, Matrix transform) noexcept {
    record(RecordedDrawMesh{.mesh = mesh, .material = material, .transform = transform});
}

void DrawText(const char* text, int posX, int posY, int fontSize, Color color) noexcept {
    record(RecordedDrawText{.text = text, .posX = posX, .posY = posY, .fontSize = fontSize, .color = color});
}

void DrawTextEx(Font font, const char* text, Vector2 position, float fontSize, float spacing, Color tint) noexcept {
    record(RecordedDrawTextEx{
        .font = font, .text = text, .position = position, .fontSize = fontSize, .spacing = spacing, .tint = tint});
}

void DrawTextPro(Font font, const char* text, Vector2 position, Vector2 origin, float rotation, float fontSize,
                 float spacing, Color tint) noexcept {
    record(RecordedDrawTextPro{.font     = font,
                               .text     = text,
                               .position = position,
                               .origin   = origin,
                               .rotation = rotation,
                               .fontSize = fontSize,
                               .spacing  = spacing,
                               .tint     = tint});
}

void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vector2 origin, float rotation,
                    Color tint) noexcept {
    record(RecordedDrawTexturePro{
        .texture = texture, .source = source, .dest = dest, .origin = origin, .rotation = rotation, .tint = tint});
}

void DrawRectanglePro(Rectangle rec, Vector2 origin, float rotation, Color color) noexcept {
    record(RecordedDrawRectanglePro{.rec = rec, .origin = origin, .rotation = rotation, .color = color});
}

void DrawCircleV(Vector2 center, float radius, Color color) noexcept {
    record(RecordedDrawCircleV{.center = center, .radius = radius, .color = color});
}

void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color) noexcept {
    record(RecordedDrawRectangleLinesEx{.rec = rec, .lineThick = lineThick, .color = color});
}

void DrawRectangleRec(Rectangle rec, Color color) noexcept {
    record(RecordedDrawRectangleRec{.rec = rec, .color = color});
}

void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick, Color color) noexcept {
    record(RecordedDrawLineEx{.startPos = startPos, .endPos = endPos, .thick = thick, .color = color});
}

void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) noexcept {
    record(RecordedDrawTriangle{.v1 = v1, .v2 = v2, .v3 = v3, .color = color});
}

void DrawRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments,
             Color color) noexcept {
    record(RecordedDrawRing{.center       = center,
                            .innerRadius  = innerRadius,
                            .outerRadius  = outerRadius,
                            .startAngle   = startAngle,
                            .endAngle     = endAngle,
                            .segments     = segments,
                            .color        = color});
}

void DrawCubeV(Vector3 position, Vector3 size, Color color) noexcept {
    record(RecordedDrawCubeV{.position = position, .size = size, .color = color});
}

void DrawCubeWiresV(Vector3 position, Vector3 size, Color color) noexcept {
    record(RecordedDrawCubeWiresV{.position = position, .size = size, .color = color});
}

void DrawGrid(int slices, float spacing) noexcept {
    record(RecordedDrawGrid{.slices = slices, .spacing = spacing});
}

void DrawCircle3D(Vector3 center, float radius, Vector3 rotationAxis, float rotationAngle, Color color) noexcept {
    record(RecordedDrawCircle3D{
        .center = center, .radius = radius, .rotationAxis = rotationAxis, .rotationAngle = rotationAngle, .color = color});
}

void DrawLine3D(Vector3 startPos, Vector3 endPos, Color color) noexcept {
    record(RecordedDrawLine3D{.startPos = startPos, .endPos = endPos, .color = color});
}

}  // namespace cactus_raylib_fake
