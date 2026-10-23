# Scene format

The deterministic UTF-8 writer emits version 2; loading accepts versions 1 and
2. Loading is transactional: failure preserves the destination scene.
Unknown records, duplicate IDs/components, invalid numbers and trailing garbage
are errors. Blank lines are allowed after the name.

```text
TINYENGINE_SCENE 2
"Scene name"
ENVIRONMENT "environments/studio_small_03_1k.hdr" 1 0 64 16 6
RENDER 1 1 1 0 2048 16 0.5 0.025 1 0 32 0.001
ENTITY 1 "Cube" T 0 1 0 0 0 0 1 1 1 1 M "meshes/showcase.gltf" "" 1 1 1
```

String fields use C++ `std::quoted` escaping for quotes/backslashes. Writers
always quote strings. Booleans are 0 or 1. Floats retain round-trip precision.

## Scene settings

Version 2 permits each optional record once, before entities. Missing records,
including in version 1, retain default settings.

```text
ENVIRONMENT "assetPath" intensity rotationRadians prefilterResolution irradianceResolution mipLevels
RENDER shadows ssao ibl compute shadowResolution pcfSamples ssaoRadius ssaoBias exposure toneMapper ssaoSamples shadowBias
```

Environment paths are relative to AssetRoot; empty restores built-in lighting.
Absolute paths, parent traversal, colons and NULs are invalid.
Intensity is finite/nonnegative and rotation finite.
Prefilter resolution is 1..512, irradiance resolution 1..128 and mip count
1..floor(log2(prefilterResolution))+1.

Shadow resolution is 1..16384, PCF samples 1..16 and SSAO samples 1..64.
SSAO radius is finite/positive; biases and exposure finite/nonnegative.
Tone mapper: 0=ACES, 1=Reinhard, 2=linear.
The compute flag describes the generic graph. Actual C++ compute dispatches
are not persisted as shader jobs.

## Entities and components

Each line starts `ENTITY id "tag"`, followed by components in any order.
Duplicate components fail. A tag and identity transform are created by default.
IDs are unique integers 1..4294967294. Destroyed IDs are never reused during
a scene lifetime, preventing stale handles from identifying later entities.

| Record | Fields |
| --- | --- |
| T | translationX Y Z, quaternionX Y Z W, scaleX Y Z |
| C | fieldOfViewDegrees, nearClip, farClip, primary |
| S | "modulePath", enabled |
| R | bodyType, mass, velocityX Y Z, gravity |
| B | halfExtentX Y Z, restitution, friction |
| M | "meshPath", "materialPath", visible, castShadows, receiveShadows |
| L | lightType, colorR G B, intensity, range, innerConeRadians, outerConeRadians, castShadows |
| A | "clipPath", volume, pitch, loop, playOnStart |

Body types: 0=static, 1=dynamic, 2=kinematic.
Light types: 0=directional, 1=point, 2=spot.
Transform numbers must be finite and quaternions nonzero.
Camera field of view is between 0 and 180 degrees, near clip positive and far
clip greater than near clip. Mass/half extents are positive.
Restitution is 0..1; friction nonnegative.
Light colors/intensity are nonnegative, range positive, and cones satisfy
0 <= inner <= outer <= pi/2. Audio volume is nonnegative and pitch positive.

Scene text is limited to 16 MiB and 100,000 entities. File/script loaders use
bounded regular-file reads (scripts: 1 MiB per module).
Asset paths are contained under AssetRoot at import, script loading, playback
and export boundaries. Runtime systems also validate live components.

Prefabs use this same format. They validate complete input before allocating
fresh IDs and copying components. Runtime identity tokens are not serialized;
replacing a scene starts a fresh script attachment lifecycle.
