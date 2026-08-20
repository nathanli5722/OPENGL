# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A personal OpenGL learning/demonstration project in C++20, built around **C++20 modules** (`.cppm` files) rather than headers. There is no test suite, linter, or CI — this is graphics-demo code you build and visually run.

## Build & run

Configure (uses the `clang-debug` preset: Ninja + clang/clang++, writes `compile_commands.json`):

```
cmake --preset clang-debug
```

Build everything:

```
cmake --build build
```

Build/run a single demo (targets: `1_hello_triangle`, `2_perspective`, `3_phong_lighting`, `4_model_loading` — numbered to match `examples/`, run in order):

```
cmake --build build --target 2_perspective
./build/2_perspective
```

Demos are windowed GLFW/OpenGL apps requiring a live display — there's no headless way to verify a change beyond "it builds and doesn't throw on startup." Shader/asset paths are resolved via the `PROJECT_SOURCE_DIR` compile definition (an absolute path baked in at configure time), so executables work from any working directory, not just the repo root.

`glfw3` is expected to already be installed on the system (`find_package(glfw3 REQUIRED)`); `glm` 1.0.1 is fetched automatically via `FetchContent` at configure time.

## Architecture

**`gl_common`** is a static library built from the `src/*.cppm` modules plus `src/glad.c`; every demo links against it. Each demo lives at `examples/<name>/main.cpp` and becomes its own executable via the `add_demo(name)` CMake function in `CMakeLists.txt` — add a new demo by dropping a directory under `examples/` and calling `add_demo` for it.

Module responsibilities (`src/`):
- **`window.cppm`** — `Window` wraps GLFW/glad init and owns the render loop. `renderLoop(F&& perloop)` computes `deltaTime` each iteration (via `glfwGetTime()`) and calls `perloop(GLFWwindow*, float deltaTime)`. `Window` does not poll input itself beyond an optional legacy `processInput` callback (unused by current demos) — each demo's per-frame lambda polls `glfwGetKey` directly and has full closure access to its own scene state (camera, uniforms, etc).
- **`buffer.cppm`** — `VertexBuffer<T>` is templated on the caller's vertex struct and takes a `std::vector<VertexAttribute>` (`{location, components, offset}`, typically built with `offsetof`) describing that struct's layout, rather than assuming a fixed vertex format. This is how position-only (`VertexData`), position+normal (`phong_lighting`'s local struct), and position+normal+uv (`ModelVertex`) vertices all share one buffer implementation.
- **`shader.cppm`** — `Shader` loads/compiles/links a vertex+fragment GLSL pair from file paths and exposes `use()` plus `setBool`/`setInt`/`setFloat`/`setMat4`/`setVec3` uniform setters.
- **`camera.cppm`** — `Camera` owns the view and projection matrices (`getViewMatrix`/`getProjectionMatrix`) and moves via `processKeyboard(CameraMovement, deltaTime)`; it does no input polling itself, matching the polling-in-the-lambda pattern above.
- **`model.cppm`** — `loadModel(path)` wraps `tinyobj::LoadObj`. Every face-vertex becomes its own `ModelVertex{x,y,z,nx,ny,nz,u,v}` with no de-duplication (the index buffer is just sequential `0..N-1`) — simple over compact. Pulls the first material's PBR fields (`Kd`→albedo, `Pm`→metallic, `Pr`→roughness, `Ke`→emissive, plus `map_*` texture paths) into `ModelMaterial`.
- **`texture.cppm`** — `Texture` wraps `stb_image` decoding + GL texture upload, move-only (deleted copy ctor/assignment) since it owns a GL handle. Takes a `ColorSpace` (`Linear`/`SRGB`) so color maps (albedo/emissive) decode as sRGB while data maps (metallic/roughness/normal) stay linear.

**Single-header vendored libraries** (`include/tiny_obj_loader.h`, `include/stb_image.h`) follow the STB pattern: declarations are included normally everywhere, but the implementation macro (`TINYOBJLOADER_IMPLEMENTATION` / `STB_IMAGE_IMPLEMENTATION`) is defined in exactly one non-module translation unit each (`src/tiny_obj_loader_impl.cpp`, `src/stb_image_impl.cpp`) so the symbols are compiled once and linked into `gl_common`.

**Error handling convention**: constructors that can fail (`Window`, `Shader`, `loadModel`, `Texture`) throw `std::runtime_error`/`std::exception`; every demo's `main()` catches at the top level, prints `"Fatal error: " + e.what()` to `stderr`, and returns 1.

Each demo's `PosNormalVertex`/`ModelVertex`-style struct and its `VertexAttribute` layout are defined locally in that demo's `main.cpp` (or in `model.cppm` for OBJ-driven ones) — there's no central vertex-format registry, by design, since `VertexBuffer<T>` doesn't need one.
