#pragma once

// Thin, compile-time-dispatched wrappers around raylib's stateful I/O surface
// (input polling, window/screen queries, drawing). Real raylib is linked
// unconditionally in every configuration; when CACTUS_RAYLIB_FAKE is not
// defined, every wrapper below is a direct passthrough to the real raylib
// function with no added indirection. When CACTUS_RAYLIB_FAKE is defined
// (headless behavioral test executables only), each wrapper instead calls
// into cactus_raylib_fake's scripted-state/recording implementation.
//
// Only functions where a headless test needs genuinely different behavior
// (scripted input/window state, or call recording) live here. Pure math
// (raymath.h), the argument-only geometry helpers (CheckCollisionPointRec,
// GetRayCollisionBox, GetModelBoundingBox, GetScreenToWorldRay), and asset
// materialization (Load*/Unload*/Gen*, shader setup) are deliberately left
// unwrapped — see design.md's "fake surface scope" decision.

#include <raylib.h>

#ifdef CACTUS_RAYLIB_FAKE
#include "fake_raylib/fake_raylib.hpp"
#endif

namespace cactus::runtime::raylib {

// ── Input (scripted when faked; never recorded) ────────────────────────────

[[nodiscard]] inline bool IsKeyDown(int key) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::IsKeyDown(key);
#else
    return ::IsKeyDown(key);
#endif
}

[[nodiscard]] inline bool IsKeyPressed(int key) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::IsKeyPressed(key);
#else
    return ::IsKeyPressed(key);
#endif
}

[[nodiscard]] inline bool IsKeyReleased(int key) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::IsKeyReleased(key);
#else
    return ::IsKeyReleased(key);
#endif
}

[[nodiscard]] inline bool IsMouseButtonDown(int button) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::IsMouseButtonDown(button);
#else
    return ::IsMouseButtonDown(button);
#endif
}

[[nodiscard]] inline bool IsMouseButtonPressed(int button) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::IsMouseButtonPressed(button);
#else
    return ::IsMouseButtonPressed(button);
#endif
}

[[nodiscard]] inline bool IsMouseButtonReleased(int button) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::IsMouseButtonReleased(button);
#else
    return ::IsMouseButtonReleased(button);
#endif
}

[[nodiscard]] inline Vector2 GetMousePosition() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::GetMousePosition();
#else
    return ::GetMousePosition();
#endif
}

[[nodiscard]] inline Vector2 GetMouseDelta() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::GetMouseDelta();
#else
    return ::GetMouseDelta();
#endif
}

[[nodiscard]] inline float GetMouseWheelMove() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::GetMouseWheelMove();
#else
    return ::GetMouseWheelMove();
#endif
}

// ── Window / environment (scripted when faked; never recorded) ────────────

[[nodiscard]] inline bool IsWindowReady() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::IsWindowReady();
#else
    return ::IsWindowReady();
#endif
}

[[nodiscard]] inline int GetScreenWidth() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::GetScreenWidth();
#else
    return ::GetScreenWidth();
#endif
}

[[nodiscard]] inline int GetScreenHeight() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    return cactus_raylib_fake::GetScreenHeight();
#else
    return ::GetScreenHeight();
#endif
}

// ── Drawing (recorded into the call log when faked) ────────────────────────

inline void ClearBackground(Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::ClearBackground(color);
#else
    ::ClearBackground(color);
#endif
}

inline void BeginMode2D(Camera2D camera) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::BeginMode2D(camera);
#else
    ::BeginMode2D(camera);
#endif
}

inline void EndMode2D() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::EndMode2D();
#else
    ::EndMode2D();
#endif
}

inline void BeginMode3D(Camera3D camera) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::BeginMode3D(camera);
#else
    ::BeginMode3D(camera);
#endif
}

inline void EndMode3D() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::EndMode3D();
#else
    ::EndMode3D();
#endif
}

inline void BeginTextureMode(RenderTexture2D target) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::BeginTextureMode(target);
#else
    ::BeginTextureMode(target);
#endif
}

inline void EndTextureMode() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::EndTextureMode();
#else
    ::EndTextureMode();
#endif
}

inline void BeginScissorMode(int x, int y, int width, int height) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::BeginScissorMode(x, y, width, height);
#else
    ::BeginScissorMode(x, y, width, height);
#endif
}

inline void EndScissorMode() noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::EndScissorMode();
#else
    ::EndScissorMode();
#endif
}

inline void DrawMesh(Mesh mesh, Material material, Matrix transform) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawMesh(mesh, material, transform);
#else
    ::DrawMesh(mesh, material, transform);
#endif
}

inline void DrawText(const char* text, int posX, int posY, int fontSize, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawText(text, posX, posY, fontSize, color);
#else
    ::DrawText(text, posX, posY, fontSize, color);
#endif
}

inline void DrawTextEx(Font font, const char* text, Vector2 position, float fontSize, float spacing,
                       Color tint) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawTextEx(font, text, position, fontSize, spacing, tint);
#else
    ::DrawTextEx(font, text, position, fontSize, spacing, tint);
#endif
}

inline void DrawTextPro(Font font, const char* text, Vector2 position, Vector2 origin, float rotation,
                        float fontSize, float spacing, Color tint) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawTextPro(font, text, position, origin, rotation, fontSize, spacing, tint);
#else
    ::DrawTextPro(font, text, position, origin, rotation, fontSize, spacing, tint);
#endif
}

inline void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vector2 origin, float rotation,
                           Color tint) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawTexturePro(texture, source, dest, origin, rotation, tint);
#else
    ::DrawTexturePro(texture, source, dest, origin, rotation, tint);
#endif
}

inline void DrawRectangleV(Vector2 position, Vector2 size, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawRectangleV(position, size, color);
#else
    ::DrawRectangleV(position, size, color);
#endif
}

inline void DrawCircleV(Vector2 center, float radius, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawCircleV(center, radius, color);
#else
    ::DrawCircleV(center, radius, color);
#endif
}

inline void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawRectangleLinesEx(rec, lineThick, color);
#else
    ::DrawRectangleLinesEx(rec, lineThick, color);
#endif
}

inline void DrawRectangleRec(Rectangle rec, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawRectangleRec(rec, color);
#else
    ::DrawRectangleRec(rec, color);
#endif
}

inline void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawLineEx(startPos, endPos, thick, color);
#else
    ::DrawLineEx(startPos, endPos, thick, color);
#endif
}

inline void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawTriangle(v1, v2, v3, color);
#else
    ::DrawTriangle(v1, v2, v3, color);
#endif
}

inline void DrawRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle,
                     int segments, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawRing(center, innerRadius, outerRadius, startAngle, endAngle, segments, color);
#else
    ::DrawRing(center, innerRadius, outerRadius, startAngle, endAngle, segments, color);
#endif
}

inline void DrawCubeV(Vector3 position, Vector3 size, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawCubeV(position, size, color);
#else
    ::DrawCubeV(position, size, color);
#endif
}

inline void DrawCubeWiresV(Vector3 position, Vector3 size, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawCubeWiresV(position, size, color);
#else
    ::DrawCubeWiresV(position, size, color);
#endif
}

inline void DrawGrid(int slices, float spacing) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawGrid(slices, spacing);
#else
    ::DrawGrid(slices, spacing);
#endif
}

inline void DrawCircle3D(Vector3 center, float radius, Vector3 rotationAxis, float rotationAngle,
                         Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawCircle3D(center, radius, rotationAxis, rotationAngle, color);
#else
    ::DrawCircle3D(center, radius, rotationAxis, rotationAngle, color);
#endif
}

inline void DrawLine3D(Vector3 startPos, Vector3 endPos, Color color) noexcept {
#ifdef CACTUS_RAYLIB_FAKE
    cactus_raylib_fake::DrawLine3D(startPos, endPos, color);
#else
    ::DrawLine3D(startPos, endPos, color);
#endif
}

}  // namespace cactus::runtime::raylib
