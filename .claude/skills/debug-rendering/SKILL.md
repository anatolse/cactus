---
name: debug-rendering
description: Use when a raylib/GLSL draw path compiles, links, and runs with no errors but renders nothing or the wrong output. Explains why headless tests cannot catch it and how to check with a real-GL screenshot harness.
---

# Diagnosing rendering issues

`CACTUS_RAYLIB_FAKE` headless tests never call `LoadShaderFromMemory` or issue a real
GL draw — a green test suite proves the generated C++ compiles, not that anything
renders. If a raylib/GLSL draw path compiles, links, and dispatches with no errors but
produces no visible output (or wrong output), don't guess at shader/uniform logic
first. Build a throwaway debug target that `#include`s the generated `.cpp` directly
(`CACTUS_GENERATED_NO_MAIN`, mirroring `tests/example_manual_generated_main.cpp`) with
a hand-written `main()` that drives a fixed number of frames against a real GL context
and calls `TakeScreenshot()`. Inspect the PNG directly instead of relying on a human to
watch the window — this turns "does it render" into a fast, repeatable loop and
isolates the failing layer (geometry vs. shader vs. draw call vs. state) by swapping
one piece at a time. Delete the harness once the bug is found; it's a diagnostic tool,
not a shipped test.
