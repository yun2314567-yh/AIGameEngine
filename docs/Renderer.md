# Renderer integration

See [RENDERING.md](RENDERING.md) for implementation, shader layouts,
environment processing, compute buffers and tested limits.

`GraphicsContext` provides the owned GLFW/Vulkan/NVRHI path used by editor and
runtime. `Renderer`, `IRenderBackend` and `VulkanNvrhiCallbacks` provide a
separate graph/adaptation API for applications owning device integration.
`NullRenderBackend` provides deterministic graph validation.
