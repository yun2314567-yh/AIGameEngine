# Requirements and validation

This checklist tracks the supplied GameEngineDoc.md against TinyEngine 0.1.0.
The engine name is TinyEngine, as confirmed by the owner. Implementation and
test evidence are separate from platform certification and release readiness.

## Implementation coverage

| Requirement | Implementation | Automated evidence |
| --- | --- | --- |
| C++ and CMake; static core, editor and runtime | C++20 `TinyEngine` library, `TinyEngineEditor`, `TinyEngineRuntime`; installed CMake package | All builds; `TinyEngineInstalledConsumer` independently includes each public header, links and runs the installed library |
| Windows, macOS and Ubuntu 24+ | Portable core and optional platform adapters; CI matrices on Windows 2022, macOS 14 and Ubuntu 24.04 | Local Windows evidence below; Linux/macOS jobs require a successful CI run |
| GLFW, Vulkan and NVRHI | GLFW windows and Vulkan/NVRHI graphics context, swapchain, render targets and resource ownership | `GpuTests.cpp`; graphical editor smoke test |
| GLM math integration | Optional GLM conversion and transform adapters; self-contained engine math API | `CoreTests.cpp`, integration configuration |
| ECS and serialized scenes | Generation-checked handles, scoped deferred mutations, components and deterministic versioned serialization | `CoreTests.cpp`, `EngineIntegrationTests.cpp`; versions 1 and 2 load |
| glTF meshes, materials and textures | glTF/GLB static triangle primitives, indexed geometry, node transforms, embedded/external buffers and images, PBR textures | `RendererTests.cpp` malformed/valid imports; GPU geometry and material comparisons |
| PBR workflow | Metallic/roughness, base color, normal, occlusion and emissive maps; material overrides; alpha mask/blend | Importer/material unit tests and GPU rendering |
| Editor transform gizmos | ImGuizmo translate/rotate/scale, hierarchy, selection and component inspectors | Editor builds and bounded graphical launch; interactive gizmo gestures need manual QA |
| Poly Haven HDRI IBL | Included CC0 Studio Small 03; HDR decode, cubemap conversion, cosine irradiance, GGX prefilter and BRDF integration | Environment tests; GPU checks both synthetic and Poly Haven illumination |
| Soft shadow maps | Directional depth map with configurable PCF samples and bias | Renderer validation and GPU shadow/no-shadow image comparison |
| SSAO | Depth/normal AO compute pass and bilateral blur | Renderer tests and GPU AO/no-AO image comparison |
| HDR and tone mapping | Floating-point lighting targets, exposure, ACES/Reinhard/linear tone mapping | CPU tone-map tests and GPU exposure comparison |
| Compute rendering | SPIR-V compute dispatch, raw UAV buffers and synchronous readback | GPU test transforms 130 words over three workgroups and rejects duplicate bindings |
| 3D physics | Bullet integration and deterministic CPU fallback; rigid bodies, box colliders, gravity, collision and velocity | Core and engine integration tests with both configurations |
| Lua update loop and component control | Module lifecycle, transform/velocity access, keyboard snapshots and prefab-based component authoring | Core scripting tests; Lua lifecycle/input integration tests |
| Safe entity creation/destruction and prefabs | Deferred mutation scopes, checked handles and serialized prefabs | Lifecycle, stale-handle, prefab and malformed-scene tests |
| Audio using miniaudio | Engine-owned playback, scene sources, listener and offline mixing | `AudioTests.cpp`; engine scene/audio integration; no physical speaker assertion |
| Editor controlled by AI agents | Documented line-command protocol for scene/entity/component/material/environment/rendering/prefab operations and export | `AutomationTests.cpp`, `EngineIntegrationTests.cpp`; `docs/AI_WORKFLOW.md` |
| Distributable stripped game | Transactional export, referenced assets, glTF dependencies, shaders, explicit private libraries and embedded dependency notices | Export validation; packaged executable launches from unrelated working directory |
| Feature showcase | `assets/scenes/FeatureShowcase.tiny` contains every serialized component; `assets/scripts/showcase.lua` exercises all ten Lua functions | Integration assertions, headless runtime showcase and graphical launches |
| Development guidance and review | `AGENTS.md`, local engine development skill, API/scene/build/renderer/agent documentation | Diff review, clean whitespace checks and focused failure-path tests |
| Repository commit and GitHub publication | Preserve the existing upstream initial commit and publish the reviewed engine source/assets | Git history and remote branch after publication |

## Reproduced locally

Windows validation on 2026-10-23 uses MSVC, CMake and an NVIDIA GeForce RTX 4060
Laptop GPU. The default configuration passes **9/9 CTest tests**. The complete
integration configuration passes **10/10 CTest tests**, enabling Vulkan, GLFW,
GLM, Lua, miniaudio, Bullet, cgltf and the editor UI together.

The integration build directory is named `build-gpu-nobullet` for historical
reasons; Bullet is enabled in the validated configuration.

Additional device/display checks run the GPU image/compute suite, the graphical
editor for three frames, and the exported graphical runtime for three frames
from a different working directory. Vulkan validation is enabled when its layer
is available. The local runs completed without reported validation warnings.
GPU comparisons establish that individual features affect the image; they are
not a visual-quality benchmark or a guarantee for every driver.

CTest logs, installed-consumer output, exported packages and GPU captures live
under ignored build directories. Build and export commands are in
`docs/BUILD_AND_EXPORT.md`. To register display-dependent checks, configure
`TINYENGINE_RUN_GPU_TESTS=ON` in a Vulkan/GLFW build.

`.github/workflows/ci.yml` defines CPU and integration matrices on all three
target operating systems, plus Vulkan/editor testing under Xvfb and lavapipe on
Ubuntu. A configured job is not evidence of a passed run. macOS Vulkan/MoltenVK
and physical Linux graphics devices have not been exercised locally.

## Current scope and release gates

- glTF supports static triangle meshes. Sparse accessors, Draco/meshopt
  compression, skeletal animation and morph targets are rejected or unsupported.
- One directional light casts shadows; point/spot shadows and cascades are not
  implemented. The shader accepts up to 32 additional light records.
- Rendering, readback and HDR preprocessing are synchronous. HDR preprocessing
  has no persistent disk cache; large environments increase startup time.
- Compute bindings currently expose raw buffers, with at most 16 distinct
  bindings and 64 MiB total per dispatch. General texture/resource binding is
  outside this API.
- Lua scripts use trusted standard libraries. There is no sandbox for untrusted
  game code. Script-created prefabs carry their authored components.
- Exports target the runtime executable's platform and architecture. Vulkan
  drivers/loaders and platform C/C++ runtimes remain system prerequisites.
  Private shared libraries must be explicitly provided. Signing, notarization
  and platform installers are separate release steps.
- Cross-platform CI, representative hardware/driver testing, extended gameplay
  sessions, interactive editor QA and performance profiling remain release gates
  before advertising a production-certified distribution.

Third-party notices are retained in `THIRD_PARTY_NOTICES.txt`, installed with
the engine and embedded in exports. The original TinyEngine source has no
project license selected; these dependency notices do not choose one.
