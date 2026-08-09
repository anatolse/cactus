#pragma once

// Test-only fake for raylib's stateful I/O surface. Called from
// cactus::runtime::raylib wrappers (src/backends/cpp-entt/raylib_io.hpp)
// only when CACTUS_RAYLIB_FAKE is defined — never linked into a real example
// executable. Function names deliberately mirror raylib's own names (just
// namespaced here) so the wrapper's fake-dispatch branch is a 1:1 mapping.

#include <raylib.h>

#include <string>
#include <variant>
#include <vector>

namespace cactus_raylib_fake {

// ── Recorded call log entries (one struct per recorded function) ──────────

struct RecordedClearBackground {
    Color color;
};
struct RecordedBeginMode2D {
    Camera2D camera;
};
struct RecordedEndMode2D {};
struct RecordedBeginMode3D {
    Camera3D camera;
};
struct RecordedEndMode3D {};
struct RecordedBeginTextureMode {
    RenderTexture2D target;
};
struct RecordedEndTextureMode {};
struct RecordedBeginScissorMode {
    int x;
    int y;
    int width;
    int height;
};
struct RecordedEndScissorMode {};
struct RecordedDrawMesh {
    Mesh mesh;
    Material material;
    Matrix transform;
};
struct RecordedDrawText {
    std::string text;
    int posX;
    int posY;
    int fontSize;
    Color color;
};
struct RecordedDrawTextEx {
    Font font;
    std::string text;
    Vector2 position;
    float fontSize;
    float spacing;
    Color tint;
};
struct RecordedDrawTextPro {
    Font font;
    std::string text;
    Vector2 position;
    Vector2 origin;
    float rotation;
    float fontSize;
    float spacing;
    Color tint;
};
struct RecordedDrawTexturePro {
    Texture2D texture;
    Rectangle source;
    Rectangle dest;
    Vector2 origin;
    float rotation;
    Color tint;
};
struct RecordedDrawRectangleV {
    Vector2 position;
    Vector2 size;
    Color color;
};
struct RecordedDrawCircleV {
    Vector2 center;
    float radius;
    Color color;
};
struct RecordedDrawRectangleLinesEx {
    Rectangle rec;
    float lineThick;
    Color color;
};
struct RecordedDrawRectangleRec {
    Rectangle rec;
    Color color;
};
struct RecordedDrawLineEx {
    Vector2 startPos;
    Vector2 endPos;
    float thick;
    Color color;
};
struct RecordedDrawTriangle {
    Vector2 v1;
    Vector2 v2;
    Vector2 v3;
    Color color;
};
struct RecordedDrawRing {
    Vector2 center;
    float innerRadius;
    float outerRadius;
    float startAngle;
    float endAngle;
    int segments;
    Color color;
};
struct RecordedDrawCubeV {
    Vector3 position;
    Vector3 size;
    Color color;
};
struct RecordedDrawCubeWiresV {
    Vector3 position;
    Vector3 size;
    Color color;
};
struct RecordedDrawGrid {
    int slices;
    float spacing;
};
struct RecordedDrawCircle3D {
    Vector3 center;
    float radius;
    Vector3 rotationAxis;
    float rotationAngle;
    Color color;
};
struct RecordedDrawLine3D {
    Vector3 startPos;
    Vector3 endPos;
    Color color;
};

using RecordedCall = std::variant<RecordedClearBackground,
                                  RecordedBeginMode2D,
                                  RecordedEndMode2D,
                                  RecordedBeginMode3D,
                                  RecordedEndMode3D,
                                  RecordedBeginTextureMode,
                                  RecordedEndTextureMode,
                                  RecordedBeginScissorMode,
                                  RecordedEndScissorMode,
                                  RecordedDrawMesh,
                                  RecordedDrawText,
                                  RecordedDrawTextEx,
                                  RecordedDrawTextPro,
                                  RecordedDrawTexturePro,
                                  RecordedDrawRectangleV,
                                  RecordedDrawCircleV,
                                  RecordedDrawRectangleLinesEx,
                                  RecordedDrawRectangleRec,
                                  RecordedDrawLineEx,
                                  RecordedDrawTriangle,
                                  RecordedDrawRing,
                                  RecordedDrawCubeV,
                                  RecordedDrawCubeWiresV,
                                  RecordedDrawGrid,
                                  RecordedDrawCircle3D,
                                  RecordedDrawLine3D>;

// ── Lifecycle ────────────────────────────────────────────────────────────

// Clears the call log and reverts all scripted input/window state to its
// default (nothing pressed/down, window not ready, default screen size).
void reset() noexcept;

// ── Input scripting ─────────────────────────────────────────────────────

void set_key_down(int key, bool is_down) noexcept;
void set_key_pressed_this_frame(int key, bool is_pressed) noexcept;
void set_key_released_this_frame(int key, bool is_released) noexcept;
void set_mouse_button_down(int button, bool is_down) noexcept;
void set_mouse_button_pressed_this_frame(int button, bool is_pressed) noexcept;
void set_mouse_button_released_this_frame(int button, bool is_released) noexcept;
void set_mouse_position(Vector2 position) noexcept;
void set_mouse_delta(Vector2 delta) noexcept;
void set_mouse_wheel_move(float delta) noexcept;

// ── Window / environment scripting ──────────────────────────────────────

// Window-ready defaults to false, matching every existing headless runtime
// test's assumption (see design.md).
void set_window_ready(bool ready) noexcept;
// Screen size defaults to 800x600.
void set_screen_size(int width, int height) noexcept;

// ── Call log access ──────────────────────────────────────────────────────

[[nodiscard]] const std::vector<RecordedCall>& call_log() noexcept;

// ── Functions matching raylib's real signatures, called by
//    cactus::runtime::raylib wrappers when CACTUS_RAYLIB_FAKE is defined ──

[[nodiscard]] bool IsKeyDown(int key) noexcept;
[[nodiscard]] bool IsKeyPressed(int key) noexcept;
[[nodiscard]] bool IsKeyReleased(int key) noexcept;
[[nodiscard]] bool IsMouseButtonDown(int button) noexcept;
[[nodiscard]] bool IsMouseButtonPressed(int button) noexcept;
[[nodiscard]] bool IsMouseButtonReleased(int button) noexcept;
[[nodiscard]] Vector2 GetMousePosition() noexcept;
[[nodiscard]] Vector2 GetMouseDelta() noexcept;
[[nodiscard]] float GetMouseWheelMove() noexcept;

[[nodiscard]] bool IsWindowReady() noexcept;
[[nodiscard]] int GetScreenWidth() noexcept;
[[nodiscard]] int GetScreenHeight() noexcept;

[[nodiscard]] Vector2 MeasureTextEx(Font font, const char* text, float fontSize, float spacing) noexcept;

void ClearBackground(Color color) noexcept;
void BeginMode2D(Camera2D camera) noexcept;
void EndMode2D() noexcept;
void BeginMode3D(Camera3D camera) noexcept;
void EndMode3D() noexcept;
void BeginTextureMode(RenderTexture2D target) noexcept;
void EndTextureMode() noexcept;
void BeginScissorMode(int x, int y, int width, int height) noexcept;
void EndScissorMode() noexcept;
void DrawMesh(Mesh mesh, Material material, Matrix transform) noexcept;
void DrawText(const char* text, int posX, int posY, int fontSize, Color color) noexcept;
void DrawTextEx(Font font, const char* text, Vector2 position, float fontSize, float spacing, Color tint) noexcept;
void DrawTextPro(Font font, const char* text, Vector2 position, Vector2 origin, float rotation, float fontSize,
                 float spacing, Color tint) noexcept;
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vector2 origin, float rotation,
                    Color tint) noexcept;
void DrawRectangleV(Vector2 position, Vector2 size, Color color) noexcept;
void DrawCircleV(Vector2 center, float radius, Color color) noexcept;
void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color) noexcept;
void DrawRectangleRec(Rectangle rec, Color color) noexcept;
void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick, Color color) noexcept;
void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) noexcept;
void DrawRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments,
             Color color) noexcept;
void DrawCubeV(Vector3 position, Vector3 size, Color color) noexcept;
void DrawCubeWiresV(Vector3 position, Vector3 size, Color color) noexcept;
void DrawGrid(int slices, float spacing) noexcept;
void DrawCircle3D(Vector3 center, float radius, Vector3 rotationAxis, float rotationAngle, Color color) noexcept;
void DrawLine3D(Vector3 startPos, Vector3 endPos, Color color) noexcept;

}  // namespace cactus_raylib_fake
