# Rendering

## Backends

`GraphicsContext` owns the actual Vulkan instance, surface, device, swapchain
and NVRHI resources. All operations/destruction use the window's owning thread.
Devices require Vulkan 1.2, timeline semaphores, synchronization2, dynamic
rendering, swapchain presentation and a combined graphics/compute queue.
Portability enumeration/subset are requested when available for MoltenVK.
Installed validation layers are enabled.

`Renderer` separately provides a backend-neutral graph/callback adapter.
`NullRenderBackend` validates lifecycle and ordering and produces no pixels.
The graphical editor/runtime use GraphicsContext.

Frames execute shadow/depth passes, SSAO compute with bilateral blur, PBR/IBL
into floating-point HDR, tone-map compute, then presentation. NVRHI transitions
resource states explicitly. A frame fence protects uploads/captures. Submission
is synchronous, prioritizing predictable ownership over multiple in-flight frames.

## Materials, lights and environments

The production cgltf path decodes glTF/GLB triangles with embedded/external
buffers and images. It preserves tangent handedness, all five PBR texture
inputs, metallic/roughness, normal scale, occlusion, emissive, alpha mask/blend
and double-sided flags. Base-color/emissive use sRGB views; data textures remain
linear. `.pbr` scalar overrides retain imported texture references.
Transparent draws sort back to front.

One directional light casts a shadow map with 1..16 Poisson PCF samples.
Up to 32 additional directional/point/spot lights contribute direct PBR lighting.
Point/spot shadows and cascaded shadows are not implemented.
SSAO uses up to 64 hemisphere samples and depth-aware bilateral blur.

Radiance HDR images load under AssetRoot. Deterministic CPU preprocessing
computes cosine-weighted diffuse irradiance, 128-sample Hammersley GGX specular
prefiltering for each mip, and a numerical 64x64 split-sum BRDF LUT.
Filtered textures use equirectangular storage with U wrap/V clamp.
Intensity/Y rotation apply in the shader.
`setEnvironment` is synchronous; failures preserve prior lighting. Empty path
restores the built-in environment. There is no offline filter disk cache.

Exposure acts on linear HDR. ACES, Reinhard and linear tone mapping produce
linear display-referred values, followed by presentation transfer encoding.
`CaptureFrame` returns completed frames as linear float RGBA.

## Shader contract

CMake compiles `.vert.hlsl`, `.frag.hlsl` and `.comp.hlsl` to Vulkan 1.2
SPIR-V using glslc or DXC. Matrices are column-major, multiplied as
`mul(matrix, vector)`. Projection is right-handed, depth [0,1], clip Y flipped.

All descriptors use set 0:

| Bindings | Purpose |
| --- | --- |
| 0, 1, 2 | Frame, object/SSAO, material constants |
| 16..20 | Base color, metallic/roughness, normal, occlusion, emissive |
| 21, 22 | Shadow depth, ambient occlusion |
| 23, 24, 25 | Prefiltered environment, irradiance, BRDF LUT |
| 32..35 | Material, clamp, comparison shadow, environment samplers |
| 48 onward | Pass-specific compute UAVs/storage images |

`ShaderTypes.h` statically checks layout.
GpuFrameConstants is 2496 bytes: five matrices at offsets 0..256,
camera/exposure at 320, feature flags at 416, light count at 432 and 32
64-byte light records at 448. Object constants are 128 bytes, material 64,
SSAO 1056 (kernel offset 32). GltfVertex retains tangent float4.

## Compute and verification

`GraphicsContext::dispatch` synchronously loads SPIR-V from the shader directory,
uploads raw buffers, dispatches and reads every buffer back in binding order.
There are at most 16 distinct bindings and 64 MiB total.
ComputeBuffer Binding 0..15 maps to HLSL u0..u15 and Vulkan 48+Binding,
using `RWByteAddressBuffer`. Shader authors must enforce buffer bounds.
Other descriptor types are outside this public dispatch API.
Validation failure preserves the output vector.

GpuTests compares geometry/material/light/shadow/SSAO/exposure/HDRI image
changes, loads synthetic and Poly Haven HDRIs, and verifies every result from
a 130-word compute job dispatched across three groups. Captures are saved in
the build directory.

Asset limits include static triangles, no skeletal animation or morph targets,
no Draco/meshopt decoding and no sparse accessors. Unsupported geometry is
rejected where it cannot be decoded safely. Assess glTF extension and device
coverage for each shipping game.
