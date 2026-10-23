# TinyEngine

TinyEngine is a C++20 3D game engine with a static core library, a graphical
editor, a separate game runtime, and a command interface for agents.
The production configuration uses GLFW, Vulkan/NVRHI, GLM, Lua, miniaudio,
Bullet, cgltf, Dear ImGui and ImGuizmo.

The renderer draws glTF geometry with PBR materials and textures, directional,
point and spot lights, PCF shadows, bilateral SSAO, HDR image based lighting,
exposure and tone mapping. Compute shaders support raw buffers and synchronous
readback. The editor provides component inspectors, transform gizmos, asset
import, scene saving, play/stop and runtime export.

## Build

The default build requires CMake 3.24+ and a C++20 compiler. It tests the
deterministic CPU implementations without downloading dependencies:

```text
cmake -S . -B build -DTINYENGINE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For the graphical editor and Vulkan rendering, install a Vulkan SDK with modern
Vulkan headers and glslc or DXC, then configure all integrations. The configure
command below can be entered on one line in any shell:

```text
cmake -S . -B build-full -DTINYENGINE_FETCH_DEPENDENCIES=ON -DTINYENGINE_ENABLE_VULKAN=ON -DTINYENGINE_ENABLE_GLFW=ON -DTINYENGINE_ENABLE_GLM=ON -DTINYENGINE_ENABLE_LUA=ON -DTINYENGINE_ENABLE_MINIAUDIO=ON -DTINYENGINE_ENABLE_BULLET=ON -DTINYENGINE_ENABLE_CGLTF=ON -DTINYENGINE_ENABLE_EDITOR_UI=ON
cmake --build build-full --config Release
ctest --test-dir build-full -C Release --output-on-failure
```

On Windows with a multi-configuration generator:

```text
build-full/editor/Release/TinyEngineEditor.exe --scene assets/scenes/FeatureShowcase.tiny --asset-root assets --runtime build-full/runtime/Release/TinyEngineRuntime.exe
build-full/runtime/Release/TinyEngineRuntime.exe --scene assets/scenes/FeatureShowcase.tiny --asset-root assets --frames 120
```

For single-configuration generators, executables are directly in
`build-full/editor` and `build-full/runtime`; pass
`-DCMAKE_BUILD_TYPE=Release` when configuring.

The feature showcase includes every serialized component and exercises the
entire ten-function Lua interface. It ships original mesh/material/audio
fixtures and a CC0 Poly Haven HDRI, with provenance in
[assets/environments/README.md](assets/environments/README.md).

## Verification and scope

The dependency-free backend checks scene, physics, asset, automation and
render-graph behavior. It reports unavailable GPU/audio/Lua integrations
explicitly. Enable `TINYENGINE_RUN_GPU_TESTS=ON` to register tests requiring
a display and Vulkan device. These compare rendered images, verify compute
buffer results and launch the graphical editor for three frames.

Windows production integrations and GPU rendering have been exercised locally.
See [requirements and validation](docs/REQUIREMENTS.md) for platform coverage
and current limits. CPU tests alone do not establish driver compatibility or
release readiness.

See [build and export](docs/BUILD_AND_EXPORT.md),
[agent workflow and Lua](docs/AI_WORKFLOW.md),
[scene format](docs/SCENE_FORMAT.md), and
[renderer contracts](docs/RENDERING.md).
