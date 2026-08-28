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

// Every wrapper below has the same shape: call into cactus_raylib_fake when
// faked, otherwise pass straight through to real raylib. These macros emit
// that shape from just the name, parameter list, and forwarded argument
// list, instead of repeating the #ifdef/#else/#endif per function; they're
// #undef'd at the end of this header since they're an implementation detail,
// not part of this header's public surface.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#ifdef CACTUS_RAYLIB_FAKE
#define CACTUS_RL_IMPL(Name, Args) cactus_raylib_fake::Name Args
#else
#define CACTUS_RL_IMPL(Name, Args) ::Name Args
#endif

#define CACTUS_RL_WRAP(Ret, Name, Params, Args)     \
    [[nodiscard]] inline Ret Name Params noexcept { \
        return CACTUS_RL_IMPL(Name, Args);          \
    }

#define CACTUS_RL_WRAP_VOID(Name, Params, Args) \
    inline void Name Params noexcept {          \
        CACTUS_RL_IMPL(Name, Args);             \
    }
// NOLINTEND(cppcoreguidelines-macro-usage)

// ── Input (scripted when faked; never recorded) ────────────────────────────

CACTUS_RL_WRAP(bool, IsKeyDown, (int key), (key))
CACTUS_RL_WRAP(bool, IsKeyPressed, (int key), (key))
CACTUS_RL_WRAP(bool, IsKeyReleased, (int key), (key))
CACTUS_RL_WRAP(bool, IsMouseButtonDown, (int button), (button))
CACTUS_RL_WRAP(bool, IsMouseButtonPressed, (int button), (button))
CACTUS_RL_WRAP(bool, IsMouseButtonReleased, (int button), (button))
CACTUS_RL_WRAP(Vector2, GetMousePosition, (), ())
CACTUS_RL_WRAP(Vector2, GetMouseDelta, (), ())
CACTUS_RL_WRAP(float, GetMouseWheelMove, (), ())
CACTUS_RL_WRAP_VOID(DisableCursor, (), ())
CACTUS_RL_WRAP_VOID(EnableCursor, (), ())

// ── Window / environment (scripted when faked; never recorded) ────────────

CACTUS_RL_WRAP(bool, IsWindowReady, (), ())
CACTUS_RL_WRAP(int, GetScreenWidth, (), ())
CACTUS_RL_WRAP(int, GetScreenHeight, (), ())

// ── Font metrics (computed when faked; never recorded) ─────────────────────
// Real MeasureTextEx depends on the loaded default font's glyph data, which a
// headless test has no way to script pixel-accurately; the fake instead
// returns a deterministic, monotonic-in-length/font-size approximation (see
// fake_raylib.cpp), sufficient to prove layout code reacts to text metrics
// without demanding cross-platform pixel-perfect acceptance (design.md's
// "Text metrics vary by font backend and platform" risk mitigation).

CACTUS_RL_WRAP(Vector2,
               MeasureTextEx,
               (Font font, const char* text, float fontSize, float spacing),
               (font, text, fontSize, spacing))

// ── Drawing (recorded into the call log when faked) ────────────────────────

CACTUS_RL_WRAP_VOID(ClearBackground, (Color color), (color))
CACTUS_RL_WRAP_VOID(BeginMode2D, (Camera2D camera), (camera))
CACTUS_RL_WRAP_VOID(EndMode2D, (), ())
CACTUS_RL_WRAP_VOID(BeginMode3D, (Camera3D camera), (camera))
CACTUS_RL_WRAP_VOID(EndMode3D, (), ())
CACTUS_RL_WRAP_VOID(BeginTextureMode, (RenderTexture2D target), (target))
CACTUS_RL_WRAP_VOID(EndTextureMode, (), ())
CACTUS_RL_WRAP_VOID(BeginScissorMode, (int x, int y, int width, int height), (x, y, width, height))
CACTUS_RL_WRAP_VOID(EndScissorMode, (), ())
CACTUS_RL_WRAP_VOID(DrawMesh, (Mesh mesh, Material material, Matrix transform), (mesh, material, transform))
CACTUS_RL_WRAP_VOID(DrawText,
                    (const char* text, int posX, int posY, int fontSize, Color color),
                    (text, posX, posY, fontSize, color))
CACTUS_RL_WRAP_VOID(DrawTextEx,
                    (Font font, const char* text, Vector2 position, float fontSize, float spacing, Color tint),
                    (font, text, position, fontSize, spacing, tint))
CACTUS_RL_WRAP_VOID(DrawTextPro,
                    (Font font,
                     const char* text,
                     Vector2 position,
                     Vector2 origin,
                     float rotation,
                     float fontSize,
                     float spacing,
                     Color tint),
                    (font, text, position, origin, rotation, fontSize, spacing, tint))
CACTUS_RL_WRAP_VOID(DrawTexturePro,
                    (Texture2D texture, Rectangle source, Rectangle dest, Vector2 origin, float rotation, Color tint),
                    (texture, source, dest, origin, rotation, tint))
CACTUS_RL_WRAP_VOID(DrawRectanglePro,
                    (Rectangle rec, Vector2 origin, float rotation, Color color),
                    (rec, origin, rotation, color))
CACTUS_RL_WRAP_VOID(DrawCircleV, (Vector2 center, float radius, Color color), (center, radius, color))
CACTUS_RL_WRAP_VOID(DrawRectangleLinesEx, (Rectangle rec, float lineThick, Color color), (rec, lineThick, color))
CACTUS_RL_WRAP_VOID(DrawRectangleRec, (Rectangle rec, Color color), (rec, color))
CACTUS_RL_WRAP_VOID(DrawLineEx,
                    (Vector2 startPos, Vector2 endPos, float thick, Color color),
                    (startPos, endPos, thick, color))
CACTUS_RL_WRAP_VOID(DrawTriangle, (Vector2 v1, Vector2 v2, Vector2 v3, Color color), (v1, v2, v3, color))
CACTUS_RL_WRAP_VOID(
    DrawRing,
    (Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color),
    (center, innerRadius, outerRadius, startAngle, endAngle, segments, color))
CACTUS_RL_WRAP_VOID(DrawCubeV, (Vector3 position, Vector3 size, Color color), (position, size, color))
CACTUS_RL_WRAP_VOID(DrawCubeWiresV, (Vector3 position, Vector3 size, Color color), (position, size, color))
CACTUS_RL_WRAP_VOID(DrawGrid, (int slices, float spacing), (slices, spacing))
CACTUS_RL_WRAP_VOID(DrawCircle3D,
                    (Vector3 center, float radius, Vector3 rotationAxis, float rotationAngle, Color color),
                    (center, radius, rotationAxis, rotationAngle, color))
CACTUS_RL_WRAP_VOID(DrawLine3D, (Vector3 startPos, Vector3 endPos, Color color), (startPos, endPos, color))

#undef CACTUS_RL_WRAP_VOID
#undef CACTUS_RL_WRAP
#undef CACTUS_RL_IMPL

}  // namespace cactus::runtime::raylib
