---
name: tinyengine-development
description: Build, test, and extend the TinyEngine C++ game engine while preserving its scene, asset, renderer, and scripting contracts.
---

# TinyEngine Development

Use this skill when changing TinyEngine engine, editor, runtime, or asset
pipeline code.

## Required workflow

1. Read `AGENTS.md`, the relevant public headers, and the associated tests
   before changing behavior.
2. Keep changes inside the owning module. Public API changes belong in
   `engine/include/TinyEngine`; implementation belongs in `engine/src`.
3. Keep the dependency-free CPU backend buildable. Third-party integrations
   must be optional, pinned, and hidden behind explicit backend boundaries.
4. Validate malformed scene and asset input before mutating live state.
5. When a component is serialized, add a round-trip test and include it in the
   Feature Showcase scene.
6. Run CMake configure, build, and CTest after changing shared contracts.
7. Before committing, review the diff for accidental generated files, unsafe
   input handling, inconsistent ownership, and claims that exceed the tested
   backend capabilities.

## API and safety rules

- Use generation-checked entity handles or guarantee identifiers are never
  reused during a scene lifetime.
- Make entity creation/destruction from scripts safe during update iteration.
- Return explicit errors for unavailable backends and malformed inputs.
- Keep serialization versioned and deterministic.
- Do not silently claim that a CPU fallback is a Vulkan renderer or that a
  placeholder parser is a production glTF importer.
