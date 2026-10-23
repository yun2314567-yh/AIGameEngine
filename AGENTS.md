# TinyEngine Development Guide

## Scope

TinyEngine is a portable C++20 engine with a static core library, an editor,
and a stripped runtime. The source of truth for behavior is the public API in
`engine/include/TinyEngine` and the serialized scene format documented in
`docs/SCENE_FORMAT.md`.

## Style

- Use PascalCase for types and public methods, camelCase for local variables,
  and `m_` for private data members.
- Keep headers self-contained and include what they use.
- Prefer `std::expected`-like explicit result objects already present in the
  codebase over exceptions at runtime boundaries.
- Keep ownership explicit with RAII and `std::unique_ptr`/`std::shared_ptr`.
- Avoid hidden global state. Engine services are owned by `Engine` and passed
  to systems explicitly.
- Public APIs must document failure behavior and thread-safety assumptions.

## Build and test

The default build is dependency-free and uses deterministic CPU fallback
implementations. Production integrations are enabled with
`-DTINYENGINE_FETCH_DEPENDENCIES=ON` plus the individual `TINYENGINE_ENABLE_*`
options. Configure and test with:

```text
cmake -S . -B build -DTINYENGINE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Every new subsystem needs focused unit tests and one integration path in
`tests/EngineIntegrationTests.cpp`. Changes that affect scene serialization
must preserve backwards-compatible loading or increment the scene version.

## Runtime safety

Entity handles are generation checked. Scripts may queue entity creation and
destruction during an update; mutations are committed after the update pass.
Asset importers reject malformed input without crashing or partially mutating
the destination asset. GPU backends report unavailable features through the
`Result` API and keep the CPU fallback usable for tests.
