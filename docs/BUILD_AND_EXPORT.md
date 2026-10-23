# Build and export

## Configurations

CMake 3.24+, a C++20 compiler and a C compiler are required. The default build
has no downloaded dependencies:

```text
cmake -S . -B build -DTINYENGINE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The graphical build enables these options in addition to
`TINYENGINE_FETCH_DEPENDENCIES=ON`: VULKAN, GLFW, GLM, LUA, MINIAUDIO, BULLET,
CGLTF and EDITOR_UI, each prefixed with `TINYENGINE_ENABLE_`.
The complete command is in the repository README.

Use a Vulkan SDK with headers compatible with the pinned NVRHI revision
(Vulkan-Headers 1.4.352 or newer), its loader, and glslc or DXC on PATH.
`VULKAN_SDK`, `Vulkan_INCLUDE_DIR`, `Vulkan_LIBRARY`,
`Vulkan_GLSLC_EXECUTABLE` and `DXC_EXECUTABLE` can select explicit locations.
The engine and NVRHI must compile against the same headers.
On macOS, install a Vulkan loader and MoltenVK and configure its ICD.
Linux additionally needs GLFW's X11/Wayland development dependencies.

`cmake/Dependencies.cmake` pins GLFW 3.4, GLM 1.0.1, miniaudio 0.11.21,
Lua 5.4.7, Bullet 3.25, cgltf 1.14, ImGui 1.91.9b-docking, ImGuizmo 1.10,
and fixed NVRHI/stb revisions. No JSON dependency is required.
CMake FetchContent source overrides support offline caches.
MSVC dependencies use a consistent /MD runtime.

`TINYENGINE_SHADER_COMPILER=AUTO|GLSLC|DXC` chooses the HLSL compiler.
The engine builds all renderer SPIR-V files. Register GPU/display tests with:

```text
cmake -S . -B build-full -DTINYENGINE_RUN_GPU_TESTS=ON
cmake --build build-full --config Release
ctest --test-dir build-full -C Release --output-on-failure
```

Install with `cmake --install build-full --config Release --prefix install`.
CMake consumers use `find_package(TinyEngine CONFIG REQUIRED)` and link
`TinyEngine::TinyEngine`. Installed files include static backend dependencies
and shaders. System loaders and platform libraries remain prerequisites.

## Editor and runtime

The graphical editor provides hierarchy/inspectors, all serialized components,
translation/rotation/scale gizmos, import, material scalars, rendering settings,
scene save/load, play/stop and export. Stopping play restores the authoring
snapshot. Commands work in either configuration:

```text
TinyEngineEditor --scene assets/scenes/FeatureShowcase.tiny --asset-root assets --commands commands.txt
```

Runtime options are `--scene <path>`, `--asset-root <directory>`,
`--shader-dir <directory>`, `--environment <relative HDR path>`,
`--frames <positive count>`, and `--headless`.
Headless execution requires a frame limit and uses a fixed 1/60 second step.
Graphical execution uses elapsed time clamped to 0.1 seconds. A window/Vulkan
initialization failure is an error. The CPU build supports bounded updates
without a display. `--frames N` also bounds the graphical editor.

Missing/invalid enabled scripts cause startup/play to fail.
Scene rendering settings and HDRI load automatically; `--environment`
overrides only the runtime environment path.

## Export contract

`EditorAutomation::exportRuntime` validates and stages the whole package before
replacing its destination. The package contains:

- The stripped runtime executable and serialized scene.
- Referenced meshes, materials, scripts, audio and HDR environment.
- External glTF buffers/images, retaining their relative paths.
- The complete renderer SPIR-V set when Vulkan is enabled.
- Explicitly supplied private shared libraries beside the executable.
- `tinyengine.manifest`, identifying the runtime, scene and assets.
- `THIRD_PARTY_NOTICES.txt`, embedded at build time so installed editors can
  preserve dependency notices without requiring the source checkout.

Missing dependencies, malformed glTF, unsafe paths, symlink redirections and copy
failures return an error and preserve an existing package. A failed publication
restores the prior package.

```cpp
TinyEngine::RuntimeExportOptions options;
options.RuntimeBinary = "build-full/runtime/Release/TinyEngineRuntime.exe";
options.OutputDirectory = "out/my-game";
options.AssetRoot = "assets";
options.ShaderDirectory = "build-full/shaders";
options.RuntimeDependencies = {}; // Supply private DLLs/.so/.dylib files.
const auto result = automation.exportRuntime(options);
```

Run the packaged executable without a scene argument. Its manifest, scene,
assets and shaders resolve relative to the executable, regardless of the working
directory. An explicit scene argument bypasses package defaults.

The package targets the platform/architecture of RuntimeBinary; export does not
cross-compile. Destination machines need a compatible Vulkan driver/loader and
platform C/C++ runtime. Signing, notarization, installers and automatic discovery
of system dependencies remain release packaging responsibilities.
